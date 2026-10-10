#include "vppcache/shader_cache.h"

#include <atomic>
#include <filesystem>
#include <fstream>

#include "vppcache/cache_paths.h"
#include "vppcache/hash.h"

namespace vppcache {

namespace {
std::atomic<uint64_t> g_hits{0};
std::atomic<uint64_t> g_misses{0};
} // namespace

std::string shaderCompileKey(const std::string& sourceText, const std::string& entryPoint,
                              const std::string& targetProfile, uint32_t compileFlags) {
    IncrementalFnv1a64 h;
    h.update(sourceText.data(), sourceText.size());
    h.update(entryPoint.data(), entryPoint.size());
    h.update(targetProfile.data(), targetProfile.size());
    h.update(&compileFlags, sizeof(compileFlags));
    return toHex16(h.finish());
}

bool tryGetCompiledShader(const std::string& key, std::vector<uint8_t>& outBytecode) {
    std::filesystem::path p = shadersCacheDir() / (key + ".bin");
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        g_misses.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    in.seekg(0, std::ios::end);
    std::streamoff len = in.tellg();
    in.seekg(0, std::ios::beg);
    if (len < 0) {
        g_misses.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    outBytecode.resize(static_cast<size_t>(len));
    if (len > 0 && !in.read(reinterpret_cast<char*>(outBytecode.data()), len)) {
        g_misses.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    g_hits.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void putCompiledShader(const std::string& key, const void* bytecode, size_t bytecodeSize) {
    std::filesystem::path finalPath = shadersCacheDir() / (key + ".bin");
    if (std::filesystem::exists(finalPath)) return;

    static std::atomic<uint64_t> tmpCounter{0};
    std::error_code ec;
    std::filesystem::path tmp = shadersCacheDir() / (key + ".tmp." +
        std::to_string(tmpCounter.fetch_add(1, std::memory_order_relaxed)));
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        if (bytecodeSize > 0) out.write(static_cast<const char*>(bytecode), static_cast<std::streamsize>(bytecodeSize));
        if (!out) { out.close(); std::filesystem::remove(tmp, ec); return; }
    }
    std::filesystem::rename(tmp, finalPath, ec);
    if (ec) std::filesystem::remove(tmp, ec);
}

ShaderCacheStats snapshotShaderCacheStats() {
    ShaderCacheStats s;
    s.hits = g_hits.load(std::memory_order_relaxed);
    s.misses = g_misses.load(std::memory_order_relaxed);
    return s;
}

void resetShaderCacheStats() {
    g_hits.store(0, std::memory_order_relaxed);
    g_misses.store(0, std::memory_order_relaxed);
}

} // namespace vppcache
