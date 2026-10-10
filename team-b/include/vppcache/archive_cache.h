#pragma once

// The persistent decoded-asset cache's archive layer (items 1+2 of the
// "make the stuff tests reload over and over already loaded" task,
// 2026-10-08).
//
// WHAT THIS GIVES A CALL SITE, drop-in: every existing `vpp::Container`-
// consuming helper (readEntry() by name, decompressEntry() by index,
// rawEntryBytes(), openNested()...) keeps working completely unchanged,
// because OpenedArchive::container IS a real vpp::Container - just one
// already wired to the disk cache below. The ONLY call-site change needed
// is replacing
//     std::vector<uint8_t> bytes = loadFile(path);
//     vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
// with
//     auto opened = vppcache::openArchive(path);
//     const vpp::Container& c = opened->container;
//
// ITEM 1 (persists ACROSS process runs): openArchive() hashes the FULL
// CONTENT of `path` (vppcache::fnv1a64, never mtime - see cache_paths.h's
// header comment on why mtime is unreliable here) and wires the Container
// to a disk-backed vpp::DecodeCache keyed by that hash. decompressEntry()
// calls on a Container built this way check the disk cache FIRST: a
// previous process (a previous `ctest` run, a previous golden check,
// anything sharing the same CRREISH_DECODED_CACHE_DIR) that already
// decoded this exact entry of this exact byte-identical archive means
// ZERO zlib work this time, just a file read of the already-decompressed
// bytes. A full miss decodes once (ordinary zlib path, unchanged) and
// persists the result for next time.
//
// ITEM 2 (within ONE process): openArchive() ALSO keeps a process-global
// registry keyed by the archive's absolute path. A second (or 50th) call
// to openArchive() for the SAME path within one process - e.g. several
// scenes/tests in one golden-check or viewer run all touching
// misc_tables.vpp_pc - returns the SAME shared_ptr instantly: no re-open,
// no re-hash, no re-touching the disk cache at all for the archive itself
// (individual decompressEntry() calls against the shared Container still
// go through the normal cache lookups, which is correct - two different
// entries of the same archive are still two different cache keys).

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "vpp/container.h"
#include "vppcache/file_blob.h"

namespace vppcache {

// Process-wide counters, for the before/after measurement harness and for
// any tool that wants to print "N archives opened, M from this process's
// own registry, K entries served from the disk cache" - see this task's
// own cold-vs-warm report for a worked example.
struct Stats {
    uint64_t archivesOpenedTotal = 0;       // every openArchive() call, hit or miss
    uint64_t archivesFromProcessRegistry = 0; // item 2 hits (same path, same process)
    uint64_t archivesHashedFresh = 0;       // item 2 misses: had to load+hash the file
    uint64_t entryCacheHits = 0;            // item 1 hits: decompressEntry() served from disk, no zlib
    uint64_t entryCacheMisses = 0;          // decompressEntry() calls that had to actually decode
    uint64_t bytesServedFromDiskCache = 0;
    uint64_t bytesWrittenToDiskCache = 0;
};
Stats snapshotStats();  // a point-in-time copy, safe to print/diff
void resetStats();      // for the measurement harness: zero counters between cold/warm runs

// One archive opened through the cache. Keep this object (or a copy of
// the shared_ptr openArchive() returns) alive for as long as any ByteView
// obtained from `container` is in use - identical lifetime contract to
// vpp::Container's own (see container.h's class comment): `blob` is what
// actually backs the original (undecoded) bytes `container` parses, and
// container.decompressEntry() results reference either a fresh heap
// buffer or (recommended, see DecodeCache::get()) the cache's own files,
// not `blob` - so in practice `blob` only needs to outlive `container`
// itself, not every decompressed result, but keeping the whole
// OpenedArchive alive is the simplest safe rule.
struct OpenedArchive {
    FileBlob blob;
    vpp::Container container;
    std::string sourceKey;      // content hash + size, e.g. "a1b2c3d4e5f6a7b8-104857600" - exposed for diagnostics/tests
    std::filesystem::path path;

    OpenedArchive(FileBlob b, vpp::DecodeCache* cache, std::string key)
        : blob(std::move(b)), container(blob.view(), cache, key), sourceKey(std::move(key)) {}
};

// THE call-site entry point - see the file-level comment above. Throws
// std::runtime_error (from FileBlob::load) if `archivePath` cannot be
// opened, or vpp::FormatError if it doesn't parse as a container (same
// exceptions ordinary `vpp::Container(ByteView)` construction would throw
// - this wrapper adds caching, not a different error contract).
std::shared_ptr<OpenedArchive> openArchive(const std::filesystem::path& archivePath);

// Drops every entry from the per-process registry (item 2) - NOT the disk
// cache (item 1). For test isolation and for the cold/warm measurement
// harness: calling this between two openArchive() calls in the SAME
// process forces the second one to re-hash and re-exercise the disk
// cache path, rather than short-circuiting via the registry.
void clearProcessRegistry();

// ----- disk-cache maintenance (bounded size, hard requirement) ----------
// Current on-disk size of the whole cache tree (archives/ + shaders/
// combined), in bytes. Walks every file under cacheRoot() - not cheap,
// call sparingly (a report/measurement tool, not a hot path).
uint64_t diskCacheSizeBytes();

// Evicts whole cached units - one archive's entire entries/ directory, or
// one cached shader bytecode file - oldest-last-touched first (an
// `atime.txt` sidecar per archive directory, and each shader file's own
// filesystem last-write-time; see archive_cache.cpp's touchAtime()) until
// the tree is at or under `maxBytes`. openArchive()/the shader cache call
// this automatically (throttled, not on every single write - see
// archive_cache.cpp) so the cache stays bounded without a separate
// maintenance step, but it is also safe and cheap enough to call directly
// (e.g. once at the end of a full ctest+golden pass, which is what this
// task's own measurement script does to report a final, enforced size).
void evictToFit(uint64_t maxBytes);

} // namespace vppcache
