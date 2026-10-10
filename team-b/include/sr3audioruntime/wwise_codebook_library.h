#pragma once

// sr3audioruntime - the real, static, compiled-in Wwise Vorbis codebook-body
// library (spec-audio-runtime.md Sec11.11.3a's MAJOR CORRECTION / Sec11.11.4,
// data transcribed in spec-audio-codebook-library.md).
//
// Sec11.11.3a's correction (2026-10-08): the main Vorbis setup-packet
// bitstream does NOT read each codebook's fields inline. For each codebook
// slot it reads a bare 10-bit index, nothing else at that position - that
// index selects a pointer into this real, static, compiled-in library of
// exactly 599 real codebook bodies (slot 599 is a NULL terminator; indices
// 600-1023 are confirmed spurious, reading off the end of the real table
// into an unrelated .data float array). The resolved pointer becomes the
// data source of a brand-new, independent bitstream cursor local to just
// that one codebook.
//
// This header declares the compiled-in copy of that library: the 599-entry
// index table (blob_offset/byte_len to slice the blob, plus 6 pre-decoded
// header fields for cross-checking this project's own parse) and the raw
// 71778-byte blob itself. Both are DATA ONLY, generated mechanically from
// spec-audio-codebook-library.md by scratchpad/gen_codebook_library.py (see
// src/wwise_codebook_library_data.cpp's own header comment) - never
// hand-edited, never re-derived from the executable directly (Team B never
// disassembles).

#include <cstddef>
#include <cstdint>

namespace sr3audioruntime {

// One real codebook body's location in kWwiseCodebookLibraryBlob, plus the
// spec's own pre-decoded header fields (spec-audio-codebook-library.md's
// index table columns) - provided purely as a cross-check for this
// project's own independent bitstream parse of the same bytes, never as a
// substitute for parsing them.
struct WwiseCodebookLibraryEntry {
    uint32_t blobOffset;
    uint32_t byteLen;
    int dim;
    int entries;
    int ordered;
    int hasLookup;
    int valueBitsIfLookup;   // 0 when hasLookup==0
    int quantvalsIfLookup;   // 0 when hasLookup==0
};

// Real codebook slots are 0..598 (599 entries). Slot 599 (the library's own
// NULL terminator) and indices 600-1023 (confirmed spurious - see this
// file's header note) are NOT valid codebook selections; a 10-bit index
// read from a real setup packet outside [0, kWwiseCodebookLibrarySize) is a
// parse error, not a codebook to look up.
constexpr int kWwiseCodebookLibrarySize = 599;
constexpr size_t kWwiseCodebookLibraryBlobSize = 71778;

extern const WwiseCodebookLibraryEntry kWwiseCodebookLibraryIndex[kWwiseCodebookLibrarySize];
extern const uint8_t kWwiseCodebookLibraryBlob[kWwiseCodebookLibraryBlobSize];

// The blob's SHA-256, as stated in spec-audio-codebook-library.md's own
// Provenance section - what verifyWwiseCodebookLibraryBlobSha256() checks
// the compiled-in blob against.
extern const char* const kWwiseCodebookLibraryBlobSha256;

// Recomputes SHA-256 over kWwiseCodebookLibraryBlob and compares it against
// kWwiseCodebookLibraryBlobSha256. Call this before trusting the table -
// per this task's own "verify the SHA-256 ... before using it - if it
// doesn't match, STOP and report rather than proceeding on corrupted data"
// requirement. Returns false on any mismatch.
bool verifyWwiseCodebookLibraryBlobSha256();

}  // namespace sr3audioruntime
