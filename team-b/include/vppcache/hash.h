#pragma once

// Fast, non-cryptographic content hashing for vppcache's cache keys (added
// 2026-10-08, "persistent decoded-asset cache" task). This is NOT a format
// reader and carries no spec citations - it exists purely so the cache can
// answer "is this the same source content as last time" without trusting
// mtimes (this project's agent-fleet worktrees/git operations do not give
// mtimes a reliable meaning - see the task notes this was written from).
//
// FNV-1a 64-bit: public-domain, well-known, single-pass, no table lookups -
// chosen for simplicity (no new third-party dependency, unlike CRC32 which
// already lives in zlib but is only 32 bits) and because collision
// resistance only needs to be "good enough to not collide across this
// project's few hundred shipped archives/entries", not cryptographic.

#include <cstddef>
#include <cstdint>
#include <string>

namespace vppcache {

constexpr uint64_t kFnv1a64Offset = 14695981039346656037ull;
constexpr uint64_t kFnv1a64Prime = 1099511628211ull;

// One-shot hash of a full byte buffer.
inline uint64_t fnv1a64(const void* data, size_t size, uint64_t seed = kFnv1a64Offset) {
    uint64_t h = seed;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= kFnv1a64Prime;
    }
    return h;
}

// Incremental hashing: feed a file (or any source) in chunks without
// holding the whole thing twice in memory, and without changing the
// result versus hashing the concatenation in one call.
class IncrementalFnv1a64 {
public:
    void update(const void* data, size_t size) { h_ = fnv1a64(data, size, h_); }
    uint64_t finish() const { return h_; }

private:
    uint64_t h_ = kFnv1a64Offset;
};

// Lowercase hex, fixed 16 characters - safe to use directly as a path
// component on every platform this project targets (Windows/Linux).
std::string toHex16(uint64_t value);

} // namespace vppcache
