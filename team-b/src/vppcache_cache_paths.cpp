#include "vppcache/cache_paths.h"

#include <cstdlib>
#include <string>

#ifndef CRREISH_DECODED_CACHE_DIR_DEFAULT
// Fallback if somehow built without the CMake-supplied define (e.g. a
// stray IDE parse pass) - ".cache" relative to the process's own current
// directory. CMakeLists.txt always supplies the real one
// (<repo>/.cache), so ordinary builds never hit this branch.
#define CRREISH_DECODED_CACHE_DIR_DEFAULT ".cache"
#endif

namespace vppcache {

namespace {

std::filesystem::path resolveRoot() {
    std::filesystem::path root;
    if (const char* env = std::getenv("CRREISH_DECODED_CACHE_DIR"); env && *env) {
        root = env;
    } else {
        root = CRREISH_DECODED_CACHE_DIR_DEFAULT;
    }
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    return root;
}

} // namespace

std::filesystem::path cacheRoot() {
    static const std::filesystem::path root = resolveRoot();
    return root;
}

std::filesystem::path archivesCacheDir() {
    std::filesystem::path p = cacheRoot() / "archives";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}

std::filesystem::path shadersCacheDir() {
    std::filesystem::path p = cacheRoot() / "shaders";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}

uint64_t maxCacheBytes() {
    if (const char* env = std::getenv("CRREISH_DECODED_CACHE_MAX_BYTES"); env && *env) {
        char* end = nullptr;
        unsigned long long v = std::strtoull(env, &end, 10);
        if (end != env && v > 0) return static_cast<uint64_t>(v);
    }
    constexpr uint64_t kDefaultMaxBytes = 2ull * 1024 * 1024 * 1024; // 2 GiB
    return kDefaultMaxBytes;
}

} // namespace vppcache
