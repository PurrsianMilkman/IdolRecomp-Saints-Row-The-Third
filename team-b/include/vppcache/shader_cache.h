#pragma once

// Persistent cache for COMPILED D3D11 shader bytecode (item 3 of the
// "make the stuff tests reload over and over already loaded" task,
// 2026-10-08).
//
// WHY BYTECODE, NOT DEVICE OBJECTS: an ID3D11VertexShader*/ID3D11PixelShader*
// is a live GPU-driver resource tied to one ID3D11Device - D3D11 defines no
// API to serialize one to disk and recreate it in a LATER process (possibly
// against a different adapter/driver/WARP build). What D3DCompile()
// actually produces, and what CreateVertexShader()/CreatePixelShader() and
// D3DReflect() actually consume, is a self-contained bytecode BLOB - that
// IS cacheable: this header persists exactly that blob, keyed by a hash of
// everything that can change its content (the HLSL source text, entry
// point, target profile, and compile flags). A cache hit skips D3DCompile()
// entirely (and, for this project's real-shader pipeline in
// tools/sr3_viewer.cpp, the D3D9-bytecode disassemble+CTAB+translate steps
// that produce the HLSL text in the first place, if the CALLER keys on the
// ORIGINAL D3D9 bytecode instead of the translated HLSL - either hashing
// scheme works with this cache, since the key is just an opaque string).
// CreateVertexShader/CreatePixelShader/D3DReflect still run every process
// (they are cheap device calls, not recompiles) - this cache only removes
// the compiler pass.
//
// Determinism note: this is pure key-value persistence of a byte-exact
// compiler OUTPUT. A cache hit returns the exact same bytes D3DCompile
// itself produced the first time it ran on this exact input, so a golden
// render using a warm shader cache is provably identical to one that just
// compiled fresh - there is no approximation or fallback path that could
// diverge.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vppcache {

// Builds the cache key from the exact D3DCompile inputs. Exposed (not
// just used internally) so a caller can compute it once and log/compare
// it, e.g. to confirm a repeat run is really hitting the same key.
std::string shaderCompileKey(const std::string& sourceText, const std::string& entryPoint,
                              const std::string& targetProfile, uint32_t compileFlags);

// Looks up `key` (see shaderCompileKey above) in the persistent cache
// under CRREISH_DECODED_CACHE_DIR/shaders/. Returns true and fills
// `outBytecode` on a hit.
bool tryGetCompiledShader(const std::string& key, std::vector<uint8_t>& outBytecode);

// Persists a freshly-compiled bytecode blob under `key` for next time.
// Best-effort: a write failure is silently ignored (exactly like the
// archive cache's put() - a cache WRITE failure must never fail the
// caller's actual compile, which already succeeded by the time this is
// called).
void putCompiledShader(const std::string& key, const void* bytecode, size_t bytecodeSize);

struct ShaderCacheStats {
    uint64_t hits = 0;
    uint64_t misses = 0;
};
ShaderCacheStats snapshotShaderCacheStats();
void resetShaderCacheStats();

} // namespace vppcache
