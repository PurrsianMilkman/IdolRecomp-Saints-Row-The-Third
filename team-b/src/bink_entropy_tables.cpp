// Defines the mutable runtime storage declared in
// include/sr3binkcodec/entropy_prestage_runtime.h - see that header's own
// top comment for the two mutually-exclusive sources (compiled-in dev data
// vs. sr3runtimedata's runtime loader).

#include "sr3binkcodec/entropy_prestage_runtime.h"

#if __has_include("sr3binkcodec/entropy_prestage_tables.h")
#include "sr3binkcodec/entropy_prestage_tables.h"
#define SR3BINKCODEC_HAVE_COMPILED_TABLES 1
#else
#define SR3BINKCODEC_HAVE_COMPILED_TABLES 0
#endif

namespace sr3binkcodec {

#if SR3BINKCODEC_HAVE_COMPILED_TABLES
std::array<uint8_t, 4> kRunLength = kRunLengthCompiled;
std::array<uint8_t, 16> kClassByte = kClassByteCompiled;
std::array<uint32_t, 16> kLutOffset = kLutOffsetCompiled;
std::array<uint8_t, 1104> kLutBlob = kLutBlobCompiled;
// 2026-10-10: kIntraQuant/kInterQuant now ALSO have compiled-in dev data
// (entropy_prestage_tables.h's kIntraQuantCompiled/kInterQuantCompiled),
// same convenience as the 4 case-selector regions above - this file's own
// prior "never compiled-in" comment is now stale for these two and has
// been removed below. Still DATA ONLY: no consumer reads these arrays yet
// (see entropy_prestage_tables.h's own top comment for why the IDCT
// butterfly itself is not implemented against them).
std::array<uint8_t, 4096> kIntraQuant = kIntraQuantCompiled;
std::array<uint8_t, 4096> kInterQuant = kInterQuantCompiled;
#else
std::array<uint8_t, 4> kRunLength{};
std::array<uint8_t, 16> kClassByte{};
std::array<uint32_t, 16> kLutOffset{};
std::array<uint8_t, 1104> kLutBlob{};
std::array<uint8_t, 4096> kIntraQuant{};
std::array<uint8_t, 4096> kInterQuant{};
#endif

namespace {
// Tracked per-region (not a single bool) so caseSelectorTablesAvailable()
// is correct regardless of which order sr3runtimedata's loader calls the
// 3 case-selector hooks in.
bool g_loadedBd40 = SR3BINKCODEC_HAVE_COMPILED_TABLES != 0;
bool g_loadedC140 = SR3BINKCODEC_HAVE_COMPILED_TABLES != 0;
bool g_loadedBd60 = SR3BINKCODEC_HAVE_COMPILED_TABLES != 0;
}  // namespace

bool caseSelectorTablesAvailable() { return g_loadedBd40 && g_loadedC140 && g_loadedBd60; }

void loadCaseSelectorRegionBd40FromRuntimeSource(const std::array<uint8_t, 16>& classByte,
                                                  const std::array<uint8_t, 4>& runLength) {
    kClassByte = classByte;
    kRunLength = runLength;
    g_loadedBd40 = true;
}

void loadCaseSelectorRegionC140FromRuntimeSource(const std::array<uint32_t, 16>& lutOffset) {
    kLutOffset = lutOffset;
    g_loadedC140 = true;
}

void loadCaseSelectorRegionBd60FromRuntimeSource(const std::array<uint8_t, 1104>& lutBlob) {
    kLutBlob = lutBlob;
    g_loadedBd60 = true;
}

void loadIntraQuantFromRuntimeSource(const std::array<uint8_t, 4096>& bytes) { kIntraQuant = bytes; }
void loadInterQuantFromRuntimeSource(const std::array<uint8_t, 4096>& bytes) { kInterQuant = bytes; }

}  // namespace sr3binkcodec
