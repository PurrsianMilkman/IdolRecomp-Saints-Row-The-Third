#pragma once

// sr3runtimedata - the runtime loader for the data regions
// spec-runtime-data-sources.md describes: the Wwise codebook-body blob
// (DX9-preferred, DX11-fallback) and the 5 binkw32.dll static lookup-table
// regions. Both were made build-time-optional by be602fb (public-source-
// export data exclusion, part 1) but, until this module, had no way to get
// real bytes back once excluded. This module closes that: for each region,
// it opens the exact install file spec-runtime-data-sources.md names, seeks
// to the exact byte offset it names, reads the exact length it names,
// computes that slice's own SHA-256, and compares it against the exact
// hash the spec names - an ordinary byte-copy operation following written
// instructions (same as this project's existing xtbl/zone-data install-
// cache readers), never by inspecting the executable directly. On a match,
// the verified bytes are wired into the SAME access points
// src/wwise_codebook_library_blob_stub.cpp / src/bink_entropy_tables.cpp
// already expose (sr3audioruntime::kWwiseCodebookLibraryBlob /
// isWwiseCodebookLibraryDataAvailable(), sr3binkcodec's kClassByte & co. /
// caseSelectorTablesAvailable()) via their own loadXFromRuntimeSource()
// hooks - this header does not invent a parallel data-access API. On a
// missing file, a short read, or a hash mismatch, the affected region is
// simply left exactly as it already was (unavailable, unless some other
// source already loaded it) - never crashes, never trusts unverified bytes.
//
// All file/offset/length/hash constants used by src/runtime_data_loader.cpp
// come ONLY from spec-runtime-data-sources.md's own stated values - this
// loader was written without opening, hexdumping, disassembling or
// otherwise directly inspecting the real game executable/DLL; it was
// validated by running the compiled loader itself against a real install
// and reading back ITS OWN computed hash, the same arm's-length discipline
// spec-runtime-data-sources.md's own "copy bytes blindly by spec" framing
// asks for.

#include <string>

namespace sr3runtimedata {

// One region's own load outcome (loaded/verified flag + a short, human-
// readable reason when it is not), for RuntimeDataLoadReport below.
struct RegionLoadStatus {
    bool loaded = false;
    std::string detail;  // e.g. "already compiled in", "dx9 sha256 match",
                          // "dx11 sha256 match (dx9 unavailable)",
                          // "file not found: <path>", "sha256 mismatch"
};

// Overall outcome of one loadRuntimeDataFromInstall()/ensureRuntimeDataLoaded()
// call - one RegionLoadStatus per data region spec-runtime-data-sources.md
// names, plus which Wwise codebook source (if any) was actually used.
struct RuntimeDataLoadReport {
    RegionLoadStatus wwiseCodebookBlob;    // Sec1 (DX9-preferred, DX11-fallback)
    RegionLoadStatus binkCaseSelectorBd40;  // Sec2 region_bd40 (kClassByte/kRunLength)
    RegionLoadStatus binkCaseSelectorC140;  // Sec2 region_c140 (kLutOffset)
    RegionLoadStatus binkCaseSelectorBd60;  // Sec2 region_bd60 (kLutBlob)
    RegionLoadStatus binkIntraQuant;        // Sec2 intra_quant (no consumer yet - see that header's own note)
    RegionLoadStatus binkInterQuant;        // Sec2 inter_quant (no consumer yet - see that header's own note)
};

// Reads installDir/SaintsRowTheThird.exe (Wwise codebook blob, DX9),
// installDir/SaintsRowTheThird_DX11.exe (same blob, DX11 fallback - tried
// only if the DX9 file is absent/unreadable/mismatched) and
// installDir/binkw32.dll (the 5 Bink static table regions), verifies each
// against spec-runtime-data-sources.md's own stated SHA-256, and on a match
// wires the bytes into the existing access points named above. Skips (and
// reports already-loaded) any region whose compiled-in data was already
// linked in, so this is always safe and cheap to call even in a normal dev
// build that already has every file compiled in.
RuntimeDataLoadReport loadRuntimeDataFromInstall(const std::string& installDir);

// Convenience matching this project's own CRREISH_GAME_CACHE_DIR
// convention (CMakeLists.txt's real_*_test family): CRREISH_GAME_INSTALL_DIR
// is a compiled-in default (a CMake cache variable, defaulting to the real
// install path on this machine, same "default to the real path, not empty"
// policy as CRREISH_GAME_CACHE_DIR) naming the directory that holds
// SaintsRowTheThird.exe/_DX11.exe/binkw32.dll. Calls the above with it.
RuntimeDataLoadReport ensureRuntimeDataLoaded();

}  // namespace sr3runtimedata
