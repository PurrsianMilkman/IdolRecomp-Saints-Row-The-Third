// Synthetic round-trip test for vppcache (the persistent decoded-asset
// cache, 2026-10-08 task): no real game data, builds tiny containers from
// scratch exactly like tests/synthetic_archive_test.cpp does, then
// exercises the cache layers built on top of vpp::Container:
//   - archive_cache.h: content-hash-keyed disk persistence (decompressEntry
//     results survive a simulated "new process"), content-hash
//     self-invalidation when the source bytes change, the within-process
//     registry (openArchive() memoizes by path), eviction/bounded size.
//   - object_registry.h: generic typed memoization.
//   - shader_cache.h: compiled-bytecode persistence.
//
// Everything runs against a private, unique temp directory
// (CRREISH_DECODED_CACHE_DIR is overridden before any cache call) so this
// test never touches - and is never affected by - this worktree's real
// .cache/ directory.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <zlib.h>

#include "vpp/container.h"
#include "vppcache/archive_cache.h"
#include "vppcache/cache_paths.h"
#include "vppcache/object_registry.h"
#include "vppcache/shader_cache.h"

namespace fs = std::filesystem;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

void writeU32LE(std::vector<uint8_t>& blob, size_t off, uint32_t v) {
    blob[off + 0] = static_cast<uint8_t>(v & 0xFF);
    blob[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    blob[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    blob[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

std::vector<uint8_t> zlibCompress(const std::vector<uint8_t>& input) {
    uLongf boundLen = compressBound(static_cast<uLong>(input.size()));
    std::vector<uint8_t> out(boundLen);
    uLongf destLen = boundLen;
    int ret = compress2(out.data(), &destLen, input.data(), static_cast<uLong>(input.size()), Z_BEST_COMPRESSION);
    if (ret != Z_OK) throw std::runtime_error("test harness: compress2 failed");
    out.resize(destLen);
    return out;
}

struct EntrySpec {
    std::string name;
    std::vector<uint8_t> payloadBytes;
    uint32_t uncompressedSize = 0;
    bool compressed = false;
};

EntrySpec rawEntry(std::string name, std::vector<uint8_t> bytes) {
    EntrySpec s;
    s.uncompressedSize = static_cast<uint32_t>(bytes.size());
    s.name = std::move(name);
    s.payloadBytes = std::move(bytes);
    s.compressed = false;
    return s;
}

EntrySpec compressedEntry(std::string name, const std::vector<uint8_t>& plaintext) {
    EntrySpec s;
    s.uncompressedSize = static_cast<uint32_t>(plaintext.size());
    s.name = std::move(name);
    s.payloadBytes = zlibCompress(plaintext);
    s.compressed = true;
    return s;
}

// Minimal re-implementation of synthetic_archive_test.cpp's own
// buildContainer() (not shared across translation units - small enough
// to duplicate rather than risk coupling two independent test files).
std::vector<uint8_t> buildContainer(const std::vector<EntrySpec>& specs) {
    uint32_t entryCount = static_cast<uint32_t>(specs.size());
    std::vector<uint8_t> nameTable;
    std::vector<uint32_t> nameOffsets;
    for (const auto& s : specs) {
        nameOffsets.push_back(static_cast<uint32_t>(nameTable.size()));
        nameTable.insert(nameTable.end(), s.name.begin(), s.name.end());
        nameTable.push_back(0);
    }
    uint32_t nameTableSize = static_cast<uint32_t>(nameTable.size());
    uint32_t dirTableSize = entryCount * 24;

    size_t dirBase = vpp::kDirectoryOffset;
    size_t nameBase = dirBase + vpp::roundUpBlock(dirTableSize);
    size_t payloadStart = nameBase + vpp::roundUpBlock(nameTableSize);

    std::vector<uint32_t> dataOffsets(specs.size());
    std::vector<size_t> physOffsets(specs.size());
    size_t logical = 0, physical = 0;
    for (size_t i = 0; i < specs.size(); ++i) {
        dataOffsets[i] = static_cast<uint32_t>(logical);
        physOffsets[i] = physical;
        logical += vpp::roundUpBlock(specs[i].uncompressedSize);
        physical += vpp::roundUpBlock(specs[i].uncompressedSize);
    }
    size_t endPhysical = 0;
    for (size_t i = 0; i < specs.size(); ++i) {
        endPhysical = std::max(endPhysical, physOffsets[i] + specs[i].payloadBytes.size());
    }
    size_t totalSize = payloadStart + endPhysical;

    std::vector<uint8_t> blob(totalSize, 0);
    writeU32LE(blob, 0x000, vpp::kMagic);
    writeU32LE(blob, 0x004, 6);
    writeU32LE(blob, 0x14C, 0);
    writeU32LE(blob, 0x154, entryCount);
    writeU32LE(blob, 0x158, static_cast<uint32_t>(totalSize));
    writeU32LE(blob, 0x15C, dirTableSize);
    writeU32LE(blob, 0x160, nameTableSize);
    writeU32LE(blob, 0x168, 0xFFFFFFFFu);

    for (uint32_t i = 0; i < entryCount; ++i) {
        size_t off = dirBase + i * 24;
        writeU32LE(blob, off + 0x00, nameOffsets[i]);
        writeU32LE(blob, off + 0x04, 0);
        writeU32LE(blob, off + 0x08, dataOffsets[i]);
        writeU32LE(blob, off + 0x0C, specs[i].uncompressedSize);
        writeU32LE(blob, off + 0x10,
                   specs[i].compressed ? static_cast<uint32_t>(specs[i].payloadBytes.size()) : vpp::kRawSentinel);
    }
    std::copy(nameTable.begin(), nameTable.end(), blob.begin() + nameBase);
    for (size_t i = 0; i < specs.size(); ++i) {
        std::copy(specs[i].payloadBytes.begin(), specs[i].payloadBytes.end(),
                   blob.begin() + payloadStart + physOffsets[i]);
    }
    return blob;
}

void writeFile(const fs::path& p, const std::vector<uint8_t>& bytes) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void setEnv(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

} // namespace

int main() {
    // Private, unique sandbox for this test run - never the real project
    // cache - set BEFORE any vppcache call (cacheRoot() memoizes its
    // resolved path in a function-local static on first use).
    fs::path sandbox = fs::temp_directory_path() /
        ("vppcache_test_" + std::to_string(static_cast<unsigned long>(std::rand())) + "_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(sandbox);
    setEnv("CRREISH_DECODED_CACHE_DIR", sandbox.string());

    fs::path cacheDir = vppcache::cacheRoot();
    CHECK(cacheDir == sandbox);

    // ---- 1. Build two DIFFERENT synthetic archives at the same path,
    //         one at a time, to test content-hash self-invalidation. ----
    std::vector<uint8_t> plaintextV1(5000);
    for (size_t i = 0; i < plaintextV1.size(); ++i) plaintextV1[i] = static_cast<uint8_t>(i * 7 + 3);
    std::vector<uint8_t> blobV1 = buildContainer(
        {rawEntry("raw_entry.txt", {'h', 'i', '\0'}), compressedEntry("data.xtbl", plaintextV1)});

    fs::path archivePath = sandbox / "fixture.vpp_pc";
    writeFile(archivePath, blobV1);

    vppcache::resetStats();

    // ---- 2. First open: full miss (hash+load fresh, zlib decode real). ----
    auto opened1 = vppcache::openArchive(archivePath);
    CHECK(opened1->container.entries().size() == 2);
    vpp::DecompressResult r1 = opened1->container.decompressEntry(1);
    CHECK(r1.status == vpp::DecodeStatus::Ok);
    CHECK(r1.data == plaintextV1);
    {
        vppcache::Stats s = vppcache::snapshotStats();
        CHECK(s.archivesHashedFresh == 1);
        CHECK(s.entryCacheMisses == 1);
        CHECK(s.entryCacheHits == 0);
    }

    // ---- 3. Same Container instance, decompress AGAIN: should now hit the
    //         disk cache this same process just wrote (still correct, just
    //         exercising the cache on the very next call). ----
    vpp::DecompressResult r1b = opened1->container.decompressEntry(1);
    CHECK(r1b.status == vpp::DecodeStatus::Ok);
    CHECK(r1b.data == plaintextV1);
    {
        vppcache::Stats s = vppcache::snapshotStats();
        CHECK(s.entryCacheHits == 1);
    }

    // ---- 4. Item 2 (within-process registry): opening the SAME path again
    //         returns the SAME shared_ptr instantly, no re-hash. ----
    auto openedAgain = vppcache::openArchive(archivePath);
    CHECK(openedAgain.get() == opened1.get());
    {
        vppcache::Stats s = vppcache::snapshotStats();
        CHECK(s.archivesFromProcessRegistry == 1);
        CHECK(s.archivesHashedFresh == 1); // unchanged - no second hash
    }

    // ---- 5. Simulate a NEW PROCESS: clear the in-memory registry (item 2
    //         state only) and reopen. The archive_cache DISK layer (item 1)
    //         is untouched by this, so the entry should come back from the
    //         DISK cache with ZERO fresh zlib work - the whole point of
    //         persisting ACROSS process runs. ----
    vppcache::clearProcessRegistry();
    vppcache::resetStats();
    auto opened2 = vppcache::openArchive(archivePath);
    CHECK(opened2.get() != opened1.get()); // genuinely a new OpenedArchive/Container
    vpp::DecompressResult r2 = opened2->container.decompressEntry(1);
    CHECK(r2.status == vpp::DecodeStatus::Ok);
    CHECK(r2.data == plaintextV1); // BYTE-IDENTICAL to the cold decode - the determinism requirement
    {
        vppcache::Stats s = vppcache::snapshotStats();
        CHECK(s.archivesHashedFresh == 1);  // still has to hash+load the file itself
        CHECK(s.entryCacheHits == 1);       // but the ENTRY came from disk, not zlib
        CHECK(s.entryCacheMisses == 0);
    }

    // ---- 6. Content-hash self-invalidation: overwrite the SAME path with
    //         DIFFERENT content, a new process-equivalent open must decode
    //         fresh (never return the old content's cached bytes). ----
    std::vector<uint8_t> plaintextV2(5000);
    for (size_t i = 0; i < plaintextV2.size(); ++i) plaintextV2[i] = static_cast<uint8_t>(i * 3 + 9);
    CHECK(plaintextV2 != plaintextV1);
    std::vector<uint8_t> blobV2 = buildContainer(
        {rawEntry("raw_entry.txt", {'h', 'i', '\0'}), compressedEntry("data.xtbl", plaintextV2)});

    // Windows will not let a file be truncated/replaced while any of this
    // PROCESS's own memory-mapped views of it are still alive (a real OS
    // restriction, ERROR_USER_MAPPED_FILE - distinct from, and not fixed
    // by, FileBlob's FILE_SHARE_* flags, which only control handle access,
    // not the "resize a mapped file" restriction). Drop every FileBlob
    // this test still holds on `archivePath` first - which is also the
    // honest equivalent of what "a new process" means for step 5 above:
    // a real new process never held the old mapping in the first place.
    // (Save opened1's sourceKey first - it is still compared below.)
    const std::string opened1SourceKey = opened1->sourceKey;
    opened1.reset();
    openedAgain.reset();
    opened2.reset();
    // The per-process REGISTRY (item 2) also holds its own owning
    // shared_ptr to whichever OpenedArchive is currently mapped to this
    // path (that is the whole point of it) - dropping the LOCAL copies
    // above is not enough on its own to release the last reference;
    // clear the registry too so the FileBlob mapping actually goes away.
    vppcache::clearProcessRegistry();
    writeFile(archivePath, blobV2);

    vppcache::clearProcessRegistry();
    vppcache::resetStats();
    auto opened3 = vppcache::openArchive(archivePath);
    CHECK(opened3->sourceKey != opened1SourceKey); // different content => different cache key
    vpp::DecompressResult r3 = opened3->container.decompressEntry(1);
    CHECK(r3.status == vpp::DecodeStatus::Ok);
    CHECK(r3.data == plaintextV2); // the NEW content, not a stale cached V1 result
    {
        vppcache::Stats s = vppcache::snapshotStats();
        CHECK(s.entryCacheMisses == 1); // genuinely new content - no stale hit
    }

    // ---- 7. object_registry.h: generic typed memoization. ----
    {
        int builds = 0;
        auto a = vppcache::getOrCreate<std::string>("k1", [&] {
            ++builds;
            return std::make_shared<std::string>("hello");
        });
        auto b = vppcache::getOrCreate<std::string>("k1", [&] {
            ++builds;
            return std::make_shared<std::string>("hello-again-should-not-run");
        });
        CHECK(a.get() == b.get());
        CHECK(*a == "hello");
        CHECK(builds == 1);
        auto c = vppcache::getOrCreate<std::string>("k2", [&] {
            ++builds;
            return std::make_shared<std::string>("world");
        });
        CHECK(c.get() != a.get());
        CHECK(builds == 2);
        vppcache::clearRegistry<std::string>();
        auto d = vppcache::getOrCreate<std::string>("k1", [&] {
            ++builds;
            return std::make_shared<std::string>("rebuilt");
        });
        CHECK(builds == 3); // cleared, so k1 had to rebuild
        CHECK(*d == "rebuilt");
    }

    // ---- 8. shader_cache.h round-trip. ----
    {
        std::string key = vppcache::shaderCompileKey("float4 main(): SV_Target { return 0; }", "main", "ps_4_0", 0);
        std::vector<uint8_t> miss;
        CHECK(!vppcache::tryGetCompiledShader(key, miss));
        std::vector<uint8_t> fakeBytecode = {1, 2, 3, 4, 5, 6, 7, 8};
        vppcache::putCompiledShader(key, fakeBytecode.data(), fakeBytecode.size());
        std::vector<uint8_t> hit;
        CHECK(vppcache::tryGetCompiledShader(key, hit));
        CHECK(hit == fakeBytecode);
    }

    // ---- 9. Bounded size / eviction. ----
    {
        uint64_t sizeBefore = vppcache::diskCacheSizeBytes();
        CHECK(sizeBefore > 0); // the archives/shader entries written above exist on disk
        vppcache::evictToFit(0); // evict EVERYTHING
        uint64_t sizeAfter = vppcache::diskCacheSizeBytes();
        CHECK(sizeAfter < sizeBefore);
        CHECK(sizeAfter == 0 || sizeAfter < sizeBefore / 2); // whole units removed, not partial
    }

    std::error_code ec;
    fs::remove_all(sandbox, ec);

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_vppcache_test: all checks passed\n";
    return 0;
}
