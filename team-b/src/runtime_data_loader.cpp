// See include/sr3runtimedata/runtime_data_loader.h for the full scope note.
//
// Every file name / byte offset / byte length / SHA-256 constant below is
// transcribed directly from spec-runtime-data-sources.md (read in full
// before writing this file) - nothing here was derived by opening,
// hexdumping or disassembling the real executable/DLL directly. Each
// constant cites the exact spec section/line it came from.

#include "sr3runtimedata/runtime_data_loader.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#include "sr3audioruntime/wwise_codebook_library.h"
#include "sr3binkcodec/entropy_prestage_runtime.h"

#ifndef CRREISH_GAME_INSTALL_DIR
#define CRREISH_GAME_INSTALL_DIR ""
#endif

namespace sr3runtimedata {
namespace {

// ---- Minimal public-domain SHA-256 (FIPS 180-4) -------------------------
// A standard, published cryptographic algorithm, not game-specific
// reverse-engineered content - same algorithm (independently written) as
// src/wwise_codebook_library.cpp's own copy, kept separate here so this
// loader has no link-time dependency the other way around.
class Sha256 {
  public:
    Sha256() { reset(); }

    void reset() {
        h_[0] = 0x6a09e667u; h_[1] = 0xbb67ae85u; h_[2] = 0x3c6ef372u; h_[3] = 0xa54ff53au;
        h_[4] = 0x510e527fu; h_[5] = 0x9b05688cu; h_[6] = 0x1f83d9abu; h_[7] = 0x5be0cd19u;
        bitLen_ = 0;
        bufLen_ = 0;
    }

    void update(const uint8_t* data, size_t len) {
        bitLen_ += static_cast<uint64_t>(len) * 8;
        while (len > 0) {
            size_t take = 64 - bufLen_;
            if (take > len) take = len;
            std::memcpy(buf_ + bufLen_, data, take);
            bufLen_ += take;
            data += take;
            len -= take;
            if (bufLen_ == 64) {
                processBlock(buf_);
                bufLen_ = 0;
            }
        }
    }

    void finalize(uint8_t out[32]) {
        uint64_t bitLenSaved = bitLen_;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t zero = 0x00;
        while (bufLen_ != 56) update(&zero, 1);
        uint8_t lenBytes[8];
        for (int i = 0; i < 8; ++i) lenBytes[i] = static_cast<uint8_t>(bitLenSaved >> (56 - 8 * i));
        std::memcpy(buf_ + 56, lenBytes, 8);
        processBlock(buf_);
        for (int i = 0; i < 8; ++i) {
            out[i * 4 + 0] = static_cast<uint8_t>(h_[i] >> 24);
            out[i * 4 + 1] = static_cast<uint8_t>(h_[i] >> 16);
            out[i * 4 + 2] = static_cast<uint8_t>(h_[i] >> 8);
            out[i * 4 + 3] = static_cast<uint8_t>(h_[i]);
        }
    }

  private:
    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void processBlock(const uint8_t block[64]) {
        static const uint32_t k[64] = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u,
        };
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4 + 0]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | static_cast<uint32_t>(block[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], hh = h_[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = hh + s1 + ch + k[i] + w[i];
            uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = s0 + maj;
            hh = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
        h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += hh;
    }

    uint32_t h_[8];
    uint64_t bitLen_;
    uint8_t buf_[64];
    size_t bufLen_;
};

std::string toHex(const uint8_t* bytes, size_t n) {
    static const char* kHexDigits = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        out.push_back(kHexDigits[bytes[i] >> 4]);
        out.push_back(kHexDigits[bytes[i] & 0xf]);
    }
    return out;
}

std::string sha256Hex(const uint8_t* data, size_t len) {
    Sha256 sha;
    sha.update(data, len);
    uint8_t digest[32];
    sha.finalize(digest);
    return toHex(digest, 32);
}

// Reads exactly `length` bytes starting at file offset `offset` from
// `path`. Returns an empty vector (distinguishable from a real empty
// region - none of this loader's regions are ever 0 bytes) on any failure
// to open, seek or read the full length - a missing/unreadable install
// file is reported this way, never by throwing or crashing.
std::vector<uint8_t> readFileRange(const std::string& path, uint64_t offset, size_t length, std::string* errorOut) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (errorOut) *errorOut = "file not found or not readable: " + path;
        return {};
    }
    f.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!f) {
        if (errorOut) *errorOut = "seek past end of file: " + path;
        return {};
    }
    std::vector<uint8_t> buf(length);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(length));
    const std::streamsize got = f.gcount();
    if (got != static_cast<std::streamsize>(length)) {
        if (errorOut) {
            std::ostringstream oss;
            oss << "short read from " << path << " at offset 0x" << std::hex << offset
                << ": wanted " << std::dec << length << " bytes, got " << got;
            *errorOut = oss.str();
        }
        return {};
    }
    return buf;
}

// One region descriptor: where to read it from, and the SHA-256 spec-
// runtime-data-sources.md states for it. `label` is only for diagnostics.
struct RegionSpec {
    const char* relativeFile;
    uint64_t offset;
    size_t length;
    const char* sha256Hex;
    const char* label;
};

// Reads+verifies one region against its own RegionSpec. On a match, returns
// the verified bytes (moved) and sets status.loaded=true with a short
// match detail; on any failure, returns empty bytes and a diagnostic
// detail, status.loaded stays false.
std::vector<uint8_t> readAndVerifyRegion(const std::string& installDir, const RegionSpec& spec,
                                          RegionLoadStatus* status) {
    const std::string path = installDir + "/" + spec.relativeFile;
    std::string err;
    std::vector<uint8_t> bytes = readFileRange(path, spec.offset, spec.length, &err);
    if (bytes.empty()) {
        status->loaded = false;
        status->detail = err;
        return {};
    }
    const std::string actualHash = sha256Hex(bytes.data(), bytes.size());
    if (actualHash != spec.sha256Hex) {
        status->loaded = false;
        status->detail = spec.label + std::string(": sha256 mismatch (expected ") + spec.sha256Hex +
                          ", got " + actualHash + ")";
        return {};
    }
    status->loaded = true;
    status->detail = spec.label + std::string(": sha256 match (") + actualHash + ")";
    return bytes;
}

}  // namespace

RuntimeDataLoadReport loadRuntimeDataFromInstall(const std::string& installDir) {
    RuntimeDataLoadReport report;

    // --- Wwise codebook blob (spec-runtime-data-sources.md Sec1) ----------
    if (sr3audioruntime::isWwiseCodebookLibraryDataAvailable()) {
        report.wwiseCodebookBlob.loaded = true;
        report.wwiseCodebookBlob.detail = "already compiled in (src/wwise_codebook_library_data.cpp)";
    } else {
        // DX9 (SaintsRowTheThird.exe), file offset 0xe7da38, length 71778 -
        // spec-runtime-data-sources.md Sec1 "DX9" subsection.
        const RegionSpec dx9{"SaintsRowTheThird.exe", 0xe7da38ull, 71778,
                              "d7ede6ddf80ab276e3c48777e20015c1b8654d17086078a2ebdf15786c4e7513", "wwise codebook (DX9)"};
        RegionLoadStatus dx9Status;
        std::vector<uint8_t> dx9Bytes = readAndVerifyRegion(installDir, dx9, &dx9Status);
        if (dx9Status.loaded) {
            sr3audioruntime::loadWwiseCodebookLibraryBlobFromRuntimeSource(dx9Bytes.data(), dx9Bytes.size());
            report.wwiseCodebookBlob.loaded = sr3audioruntime::isWwiseCodebookLibraryDataAvailable();
            report.wwiseCodebookBlob.detail = dx9Status.detail;
        } else {
            // DX11 fallback (SaintsRowTheThird_DX11.exe), file offset
            // 0xe815e8, length 71778 - spec-runtime-data-sources.md Sec1
            // "DX11" subsection, explicitly lower-confidence per that
            // section's own caveat (derived by byte-pattern search against
            // one real copy, not cross-checked against DX11 disassembly).
            const RegionSpec dx11{"SaintsRowTheThird_DX11.exe", 0xe815e8ull, 71778,
                                   "5815d2c9a96efad86abc04661bd54cf350aed5011036d3f794753c94999b64cb",
                                   "wwise codebook (DX11 fallback)"};
            RegionLoadStatus dx11Status;
            std::vector<uint8_t> dx11Bytes = readAndVerifyRegion(installDir, dx11, &dx11Status);
            if (dx11Status.loaded) {
                sr3audioruntime::loadWwiseCodebookLibraryBlobFromRuntimeSource(dx11Bytes.data(), dx11Bytes.size());
                report.wwiseCodebookBlob.loaded = sr3audioruntime::isWwiseCodebookLibraryDataAvailable();
                report.wwiseCodebookBlob.detail = "dx9 unavailable (" + dx9Status.detail + "); " + dx11Status.detail;
            } else {
                report.wwiseCodebookBlob.loaded = false;
                report.wwiseCodebookBlob.detail = "dx9: " + dx9Status.detail + "; dx11: " + dx11Status.detail;
            }
        }
    }

    // --- Bink static tables (spec-runtime-data-sources.md Sec2) ----------
    if (sr3binkcodec::caseSelectorTablesAvailable()) {
        report.binkCaseSelectorBd40 = {true, "already compiled in (region_bd40)"};
        report.binkCaseSelectorC140 = {true, "already compiled in (region_c140)"};
        report.binkCaseSelectorBd60 = {true, "already compiled in (region_bd60)"};
    } else {
        // region_bd40 (0x0002c940, 96 bytes): bytes 4-19 = kClassByte (16),
        // bytes 20-23 = kRunLength (4). Leading/trailing bytes (0-3, 24-95)
        // are read as part of the verified 96-byte region but not sliced
        // into either table - spec-runtime-data-sources.md Sec2's own
        // layout note says their role is not fully pinned down upstream.
        const RegionSpec bd40{"binkw32.dll", 0x0002c940ull, 96,
                               "6b5955deb66e92654c54d324ba92dd511b8a4fae4cd41a9eca0e3859f154f135", "region_bd40"};
        std::vector<uint8_t> bd40Bytes = readAndVerifyRegion(installDir, bd40, &report.binkCaseSelectorBd40);
        if (report.binkCaseSelectorBd40.loaded) {
            std::array<uint8_t, 16> classByte{};
            std::array<uint8_t, 4> runLength{};
            std::memcpy(classByte.data(), bd40Bytes.data() + 4, 16);
            std::memcpy(runLength.data(), bd40Bytes.data() + 20, 4);
            sr3binkcodec::loadCaseSelectorRegionBd40FromRuntimeSource(classByte, runLength);
        }

        // region_c140 (0x0002cd40, 64 bytes): 16 little-endian uint32
        // offsets into region_bd60 -> kLutOffset.
        const RegionSpec c140{"binkw32.dll", 0x0002cd40ull, 64,
                               "5a694c700b14d5e3b861c3aaa5921c34ad77d786d8e5fb6ed4f9b2ee944c0af4", "region_c140"};
        std::vector<uint8_t> c140Bytes = readAndVerifyRegion(installDir, c140, &report.binkCaseSelectorC140);
        if (report.binkCaseSelectorC140.loaded) {
            std::array<uint32_t, 16> lutOffset{};
            for (int i = 0; i < 16; ++i) {
                const uint8_t* p = c140Bytes.data() + static_cast<size_t>(i) * 4;
                lutOffset[static_cast<size_t>(i)] = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                                                     (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
            }
            sr3binkcodec::loadCaseSelectorRegionC140FromRuntimeSource(lutOffset);
        }

        // region_bd60 (0x0002c960, 1104 bytes): the shared variable-record
        // blob region_c140's offsets index into -> kLutBlob, embedded in
        // full (same "preserve the whole verified region" rule as the
        // compiled-in table uses).
        const RegionSpec bd60{"binkw32.dll", 0x0002c960ull, 1104,
                               "a8ac69960cdddc8daa498b0412d9ce6587c4773e21fec12c6d43735f88179e97", "region_bd60"};
        std::vector<uint8_t> bd60Bytes = readAndVerifyRegion(installDir, bd60, &report.binkCaseSelectorBd60);
        if (report.binkCaseSelectorBd60.loaded) {
            std::array<uint8_t, 1104> lutBlob{};
            std::memcpy(lutBlob.data(), bd60Bytes.data(), 1104);
            sr3binkcodec::loadCaseSelectorRegionBd60FromRuntimeSource(lutBlob);
        }
    }

    // intra_quant / inter_quant (0x000288c0 / 0x000298c0, 4096 bytes each) -
    // no consumer in this project yet (see entropy_prestage_runtime.h's own
    // note: the real-DCT block engines that would read these are still an
    // OPEN hook), so there is no "already compiled in" fast path and no
    // availability flag to gate on - always attempted fresh, loaded purely
    // so the verified bytes are ready for whenever that engine lands.
    {
        const RegionSpec intraQuant{"binkw32.dll", 0x000288c0ull, 4096,
                                     "74ea56443cd480620a0d69402c6a796d8cd1715c91259d11522803eb6b8c3ca8", "intra_quant"};
        std::vector<uint8_t> bytes = readAndVerifyRegion(installDir, intraQuant, &report.binkIntraQuant);
        if (report.binkIntraQuant.loaded) {
            std::array<uint8_t, 4096> arr{};
            std::memcpy(arr.data(), bytes.data(), 4096);
            sr3binkcodec::loadIntraQuantFromRuntimeSource(arr);
        }
    }
    {
        const RegionSpec interQuant{"binkw32.dll", 0x000298c0ull, 4096,
                                     "4442cb0c6670d276dde6a5cbf51ba4567819cd916d5036f0f306a34c1351404b", "inter_quant"};
        std::vector<uint8_t> bytes = readAndVerifyRegion(installDir, interQuant, &report.binkInterQuant);
        if (report.binkInterQuant.loaded) {
            std::array<uint8_t, 4096> arr{};
            std::memcpy(arr.data(), bytes.data(), 4096);
            sr3binkcodec::loadInterQuantFromRuntimeSource(arr);
        }
    }

    return report;
}

RuntimeDataLoadReport ensureRuntimeDataLoaded() {
    return loadRuntimeDataFromInstall(CRREISH_GAME_INSTALL_DIR);
}

}  // namespace sr3runtimedata
