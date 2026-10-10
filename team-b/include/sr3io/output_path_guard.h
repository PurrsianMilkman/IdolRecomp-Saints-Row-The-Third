// Output-path guard for this project's capture writers (writePng in
// src/png_writer.cpp, writeWavPcm16 in src/wav_writer.cpp).
//
// Why this exists (2026-10-08 incident, confirmed by hex dump + MD5, not a
// guess): the shared game archive
//   D:\Project Crreish\Saints Row 3 CRREISH\packfiles\pc\cache\sr3_city_0.vpp_pc
// (normally ~1.5GB) was found overwritten by a 40584-byte 800x600 PNG whose
// bytes are byte-for-byte identical to this repo's own committed golden
// tests/golden/clmesh_airport_controltower_lit/baseline_capture.png. That
// golden is captured by tools/prototype_lit_clmesh_tower2.cpp, whose
// argv[1] is taken as the raw output path (line 1104,
// `if (argc >= 2) outPath = argv[1];`) and handed straight to
// sr3render::writePng() (line 2632) with no validation. Some invocation
// passed the live archive path as argv[1]. Every one of the ~11 tools that
// call writePng() has that same "argv straight into the writer" shape, so
// the guard lives once, inside the writers, instead of at each call site.
//
// isOutputPathSafe() refuses a write when ANY of these hold:
//   (a) the resolved (absolute, weakly-canonical - so "..", symlinks and
//       junctions on the existing part of the path are resolved) path lies
//       inside a protected game-install root. Comparison is ASCII
//       case-insensitive, separator-normalized ('\\' -> '/'), and on whole
//       path components (so ".../Saints Row 3 CRREISH Backup/x.png" is NOT
//       treated as inside ".../Saints Row 3 CRREISH/").
//   (b) a file already exists at that path and its extension is not the
//       writer's expected one (".png" / ".wav", case-insensitive) - checked
//       on both the requested and the resolved path, so a "foo.png" symlink
//       onto a .vpp_pc is still refused. Refuses clobbering .vpp_pc,
//       .str2_pc, .cpeg_pc, .fxo_pc, ... anywhere, root list or not.
//   (c) a file already exists at that path and is larger than the writer's
//       size ceiling (kMaxOverwriteBytesPng / kMaxOverwriteBytesWav) - a
//       last line of defence against clobbering something big even if (a)
//       and (b) both miss (e.g. a hard link, or an archive copy renamed
//       .png).
// A path that does not exist yet is only subject to (a).
//
// Header-only on purpose: sr3render (png_writer.cpp, WIN32-only) and
// sr3audioruntime (wav_writer.cpp, cross-platform) both include it without
// a new library target, and any tool with its own private writer can adopt
// it with one #include.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace sr3io {

// Size ceilings for overwriting an EXISTING file. Measured 2026-10-08: the
// largest .png/.wav anywhere under this repo (all worktrees, results/,
// tests/golden/) is 1,482,344 bytes; frozen goldens top out at 1,352,984.
// PNG: 8 MiB leaves >5x headroom. WAV: 64 MiB (~6 min of 44.1kHz 16-bit
// stereo PCM) since a full decoded music/dialogue stream is a legitimate
// capture far larger than any frame PNG; still 1/24th of the clobbered
// 1.5GB archive.
constexpr std::uintmax_t kMaxOverwriteBytesPng = 8ull * 1024 * 1024;
constexpr std::uintmax_t kMaxOverwriteBytesWav = 64ull * 1024 * 1024;
// sr3save_NN.sr3s_pc (spec-save-format.md Sec1/Sec11.1): the real snapshot is
// exactly 108,776 bytes; 1 MiB leaves ~9x headroom for a from-spec writer
// (src/lua_save_game_bridge.cpp) without coming anywhere near this guard's
// real purpose - refusing to clobber something orders of magnitude bigger,
// like the 1.5GB archive this guard's own top note describes.
constexpr std::uintmax_t kMaxOverwriteBytesSr3s = 1ull * 1024 * 1024;

// The game-install roots no capture writer may ever write under. The first
// is the install every tool here reads (the parent of the
// "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache" every
// tools/*.cpp hardcodes - the root, not packfiles/, so sibling folders like
// videos/ are covered too). The second is the intact install copy the
// 2026-10-08 sr3_city_0.vpp_pc was restored from.
inline const std::vector<std::string>& defaultProtectedOutputRoots() {
    static const std::vector<std::string> roots = {
        "D:/Project Crreish/Saints Row 3 CRREISH",
        "C:/Games/Saints Row 3 CRREISH",
    };
    return roots;
}

namespace detail {

inline std::string lowerAsciiSlashes(std::string s) {
    for (char& c : s) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    // "\\?\C:\x" long-path prefix -> "c:/x".
    if (s.rfind("//?/", 0) == 0) s.erase(0, 4);
    return s;
}

// Absolute, weakly-canonical, normalized-for-comparison form of `p`. Never
// throws; falls back to a lexical normalization if canonicalization fails.
inline std::string resolveForCompare(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::path abs = std::filesystem::absolute(p, ec);
    if (ec) abs = p;
    std::filesystem::path resolved = std::filesystem::weakly_canonical(abs, ec);
    if (ec) resolved = abs.lexically_normal();
    std::string s = lowerAsciiSlashes(resolved.generic_u8string());
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

// True when `resolvedPath` equals `resolvedRoot` or lies under it on a
// whole-component boundary. Both arguments must come from
// resolveForCompare().
inline bool isUnderRoot(const std::string& resolvedPath, const std::string& resolvedRoot) {
    if (resolvedRoot.empty()) return false;
    if (resolvedPath.size() < resolvedRoot.size()) return false;
    if (resolvedPath.compare(0, resolvedRoot.size(), resolvedRoot) != 0) return false;
    if (resolvedPath.size() == resolvedRoot.size()) return true;
    return resolvedRoot.back() == '/' || resolvedPath[resolvedRoot.size()] == '/';
}

inline std::string lowerExtension(const std::filesystem::path& p) {
    return lowerAsciiSlashes(p.extension().generic_u8string());
}

} // namespace detail

// Returns true when `path` is safe for a capture writer whose output
// extension is `expectedExtension` (e.g. ".png", including the dot) to
// create/overwrite. Returns false and sets `reason` to a human-readable
// explanation otherwise. Reads filesystem metadata only; never writes and
// never throws. `protectedRoots` defaults to defaultProtectedOutputRoots();
// tests pass a synthetic temp-dir root instead.
inline bool isOutputPathSafe(const std::string& path, const std::string& expectedExtension,
                             std::uintmax_t maxExistingBytes, std::string& reason,
                             const std::vector<std::string>& protectedRoots = defaultProtectedOutputRoots()) {
    namespace fs = std::filesystem;
    // Narrow-string constructor (ANSI code page on MSVC), NOT u8path: the
    // writers open `path.c_str()` via fopen_s/std::ofstream, which use that
    // same interpretation, so the guard checks exactly the file the writer
    // would open.
    const fs::path requested = fs::path(path);
    const std::string resolved = detail::resolveForCompare(requested);
    const std::string refusePrefix = "refusing to write '" + path + "' (resolved '" + resolved + "'): ";

    // (a) inside a protected game-install root.
    for (const std::string& root : protectedRoots) {
        const std::string resolvedRoot = detail::resolveForCompare(fs::path(root));
        if (detail::isUnderRoot(resolved, resolvedRoot)) {
            reason = refusePrefix + "path lies inside the protected game-install root '" + root +
                     "' - capture output must never be written into the game data tree";
            return false;
        }
    }

    std::error_code ec;
    const fs::file_status st = fs::status(requested, ec);
    if (ec || !fs::exists(st)) return true; // nothing there to clobber
    if (fs::is_directory(st)) return true;  // the open itself will fail; not our concern

    // (b) existing file with the wrong extension.
    const std::string want = detail::lowerAsciiSlashes(expectedExtension);
    const std::string reqExt = detail::lowerExtension(requested);
    const std::string resExt = detail::lowerExtension(fs::path(resolved));
    if (reqExt != want || resExt != want) {
        reason = refusePrefix + "an existing file is there with extension '" +
                 (reqExt != want ? reqExt : resExt) + "', not '" + want +
                 "' - refusing to overwrite a file this writer did not produce";
        return false;
    }

    // (c) existing file larger than this writer's ceiling.
    const std::uintmax_t size = fs::file_size(requested, ec);
    if (!ec && size > maxExistingBytes) {
        reason = refusePrefix + "the existing file is " + std::to_string(size) +
                 " bytes, over this writer's " + std::to_string(maxExistingBytes) +
                 "-byte overwrite ceiling - far larger than any capture this project produces";
        return false;
    }
    return true;
}

} // namespace sr3io
