#pragma once

// Where the persistent decoded-asset cache lives on disk (2026-10-08,
// "persistent decoded-asset cache" task).
//
// NAMING NOTE, important: this task's own brief named the env var
// `CRREISH_GAME_CACHE_DIR`. That exact name is ALREADY a load-bearing
// CMake cache variable in this project (CMakeLists.txt, ~line 1777),
// meaning something different and pre-existing: the directory holding the
// *raw shipped* `.vpp_pc` archives that every `real_*_test` reads from
// (compiled in via `target_compile_definitions(... CRREISH_GAME_CACHE_DIR=
// "${CRREISH_GAME_CACHE_DIR}")`, referenced in 7 test .cpp files plus
// CMakeLists.txt's own `CRREISH_VERIFY_BUILD` gating logic). Reusing that
// identical name for this unrelated, NEW purpose (a derived/decoded-bytes
// cache this task writes INTO) would silently collide: the same string
// would mean "read shipped archives from here" in one place and "write
// decoded cache files here" in another, with no compiler or runtime check
// to catch a mix-up. Rather than overload a pre-existing, heavily-
// referenced variable with a second, incompatible meaning, this task uses
// a distinctly-named env var instead: `CRREISH_DECODED_CACHE_DIR`. This is
// a deliberate, documented deviation from the task brief's literal name,
// made after verifying the collision against the actual source tree
// (project convention: relayed instructions get verified, not blindly
// applied, when they conflict with what the repo actually contains).

#include <cstdint>
#include <filesystem>

namespace vppcache {

// Resolves the cache root directory:
//   1. $CRREISH_DECODED_CACHE_DIR, if set (lets a developer/CI point the
//      cache anywhere, or share one cache dir across worktrees);
//   2. else the compiled-in default, `<repo>/.cache` (gitignored - see
//      .gitignore), computed at CMake configure time from
//      CMAKE_CURRENT_SOURCE_DIR so it stays correct per-worktree without
//      needing an env var set at all.
// Creates the directory (and `archives/`, `shaders/` beneath it) if
// missing. Thread-safe (directory creation is idempotent/racy-safe via
// std::filesystem::create_directories).
std::filesystem::path cacheRoot();
std::filesystem::path archivesCacheDir();
std::filesystem::path shadersCacheDir();

// Bounded-size hard requirement: the maximum total number of bytes the
// cache tree (archives/ + shaders/ combined) is allowed to occupy.
// $CRREISH_DECODED_CACHE_MAX_BYTES overrides the default (2 GiB) - see
// archive_cache.h's evictToFit()/enforceBudget().
uint64_t maxCacheBytes();

} // namespace vppcache
