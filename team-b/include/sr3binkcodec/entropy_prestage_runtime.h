#pragma once

// sr3binkcodec - mutable runtime storage for the 5 Bink static lookup-table
// regions (spec-bink-static-tables.md's data appendix; locations/hashes
// restated, no bytes, in spec-runtime-data-sources.md Sec2).
//
// Replaces the pre-existing __has_include-based compile-time switch
// (entropy_prestage.h/entropy_prestage_tables.h, be602fb) with a single
// always-compiled set of mutable extern symbols, defined once in
// src/bink_entropy_tables.cpp. That .cpp fills them from exactly one of two
// mutually-exclusive sources per process:
//
//   1. entropy_prestage_tables.h's own compiled-in *Compiled constants (dev
//      convenience - present in every non-public checkout today, EXISTS-
//      gated at configure time same as before); or
//   2. sr3runtimedata's runtime loader (include/sr3runtimedata/
//      runtime_data_loader.h), which reads + independently SHA-256-verifies
//      binkw32.dll directly from the player's own real game install (see
//      spec-runtime-data-sources.md Sec2 for the exact file/offset/length/
//      hash per region) and calls the loadXFromRuntimeSource() hooks below
//      - never with unverified bytes.
//
// caseSelectorTablesAvailable() reports true once the 3 regions
// buildCaseSelectorTable()/CaseSelectorScratchSource actually read
// (region_bd40 -> kClassByte/kRunLength, region_c140 -> kLutOffset,
// region_bd60 -> kLutBlob) have all been populated by either source -
// callers never need to know or care which. kIntraQuant/kInterQuant
// (region intra_quant/inter_quant) are loader-only (no compiled-in literal
// for these two has ever existed in this project - the real-DCT block
// engines that would consume them are still an OPEN hook, see
// frame_decoder.h's own top comment) - verified and stored the same way,
// with no separate availability flag since nothing reads them yet.

#include <array>
#include <cstdint>

namespace sr3binkcodec {

extern std::array<uint8_t, 4> kRunLength;
extern std::array<uint8_t, 16> kClassByte;
extern std::array<uint32_t, 16> kLutOffset;
extern std::array<uint8_t, 1104> kLutBlob;
extern std::array<uint8_t, 4096> kIntraQuant;
extern std::array<uint8_t, 4096> kInterQuant;

// True once region_bd40 (kClassByte/kRunLength), region_c140 (kLutOffset)
// and region_bd60 (kLutBlob) have all been populated (compiled-in, or by a
// SHA-256-verified runtime load). False otherwise - CaseSelectorScratchSource
// ::beginBlockRow() (entropy_prestage.h) checks this first and reports
// CaseSelectorStatus::TablesNotLoaded instead of building a table from the
// zeroed placeholder.
bool caseSelectorTablesAvailable();

// Loader-only wiring hooks - called only by sr3runtimedata's runtime loader,
// each only after ITS OWN independent SHA-256 check of the freshly-read
// bytes against spec-runtime-data-sources.md Sec2's stated hash for that
// exact region has already passed. Not a second read API: every caller
// outside the loader still only ever reads via the extern arrays and
// caseSelectorTablesAvailable() above.
void loadCaseSelectorRegionBd40FromRuntimeSource(const std::array<uint8_t, 16>& classByte,
                                                  const std::array<uint8_t, 4>& runLength);
void loadCaseSelectorRegionC140FromRuntimeSource(const std::array<uint32_t, 16>& lutOffset);
void loadCaseSelectorRegionBd60FromRuntimeSource(const std::array<uint8_t, 1104>& lutBlob);
void loadIntraQuantFromRuntimeSource(const std::array<uint8_t, 4096>& bytes);
void loadInterQuantFromRuntimeSource(const std::array<uint8_t, 4096>& bytes);

}  // namespace sr3binkcodec
