// Fallback for src/wwise_codebook_library_data.cpp's own 71778-byte
// kWwiseCodebookLibraryBlob (see that file's and
// include/sr3audioruntime/wwise_codebook_library.h's own header comments) -
// linked in INSTEAD of that file whenever it is absent (CMakeLists.txt's
// EXISTS guard), as it is in this project's public source export, which
// excludes the game/middleware DATA it holds.
//
// Unlike the pre-runtime-loader stub this replaces (be602fb), this is NOT a
// dumb always-false zeroed placeholder: kWwiseCodebookLibraryBlob below is
// mutable storage, zeroed only UNTIL sr3runtimedata's runtime loader
// (include/sr3runtimedata/runtime_data_loader.h) reads the real 71778 bytes
// directly from the player's own verified game install (SaintsRowTheThird
// .exe, DX9-preferred/DX11-fallback - see spec-runtime-data-sources.md
// Sec1), independently SHA-256-verifies them, and calls
// loadWwiseCodebookLibraryBlobFromRuntimeSource() below - never with
// unverified bytes. kWwiseCodebookLibraryBlobSha256 here is the REAL
// expected hash (the DX9 value spec-audio-codebook-library.md's own
// Provenance section states, same constant src/
// wwise_codebook_library_data.cpp uses), not a deliberately-wrong sentinel -
// verifyWwiseCodebookLibraryBlobSha256() then genuinely computes and
// compares, rather than being guaranteed to fail by construction; against
// the zeroed placeholder (before any load attempt) it still correctly
// mismatches, for the ordinary reason that zero bytes don't hash to a real
// codebook blob's hash.
//
// The 599-row kWwiseCodebookLibraryIndex table (blobOffset/byteLen etc.) is
// NOT part of this file any more - it always compiles in separately via
// src/wwise_codebook_library_index_data.cpp (see that file's own header
// comment for why: it is derived analysis metadata, not excluded game
// bytes, per spec-runtime-data-sources.md's own framing).

#include "sr3audioruntime/wwise_codebook_library.h"

#include <algorithm>
#include <cstring>

namespace sr3audioruntime {

uint8_t kWwiseCodebookLibraryBlob[kWwiseCodebookLibraryBlobSize] = {};

const char* const kWwiseCodebookLibraryBlobSha256 = "d7ede6ddf80ab276e3c48777e20015c1b8654d17086078a2ebdf15786c4e7513";

namespace {
bool g_wwiseCodebookBlobLoaded = false;
}  // namespace

bool isWwiseCodebookLibraryDataAvailable() { return g_wwiseCodebookBlobLoaded; }

void loadWwiseCodebookLibraryBlobFromRuntimeSource(const uint8_t* verifiedBytes, size_t len) {
    if (verifiedBytes == nullptr || len != kWwiseCodebookLibraryBlobSize) {
        // Defensive only - sr3runtimedata's loader never calls this with a
        // short/null buffer (it only calls here after confirming it read
        // exactly kWwiseCodebookLibraryBlobSize bytes and SHA-256-verified
        // them); a caller that somehow did would leave the blob exactly as
        // it was before (zeroed/unavailable, or a prior real load kept),
        // never partially overwritten.
        return;
    }
    std::memcpy(kWwiseCodebookLibraryBlob, verifiedBytes, len);
    g_wwiseCodebookBlobLoaded = true;
}

}  // namespace sr3audioruntime
