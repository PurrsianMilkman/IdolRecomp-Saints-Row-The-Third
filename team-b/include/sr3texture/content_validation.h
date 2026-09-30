#pragma once

// Content-level corroboration for Container::decompressEntry results on
// .cpeg_pc/.cvbm_pc entries - the same pattern as vpp::refineWithXtblValidation
// and sr3fxo::refineWithFxoValidation, applied to this format's own
// structural check (spec-texture-format.md Sec2-3.1's confirmed exact-fit
// invariants). Lives in sr3texture rather than vpp to keep the core
// container library free of per-payload-format knowledge; depends
// one-directionally on vpp_container for the shared
// DecompressResult/DecodeStatus types.
//
// Only .cpeg_pc/.cvbm_pc (the CPU-side file, which has real structure) is
// validated here - NOT .gpeg_pc/.gvbm_pc, which per spec Sec1 is just raw,
// unstructured, back-to-back pixel data with no header of its own, so
// there is nothing this format's own bytes could self-check against
// without also having the paired .cpeg_pc's record data in hand (which
// this per-entry validation hook doesn't have access to).

#include <string>

#include "vpp/container.h"

namespace sr3texture {

// True if `filename` names a .cpeg_pc or .cvbm_pc paired-texture CPU-side
// file (spec-texture-format.md Sec7: .cvbm_pc is confirmed to be this
// exact same format under a different extension). Deliberately does NOT
// match .gpeg_pc/.gvbm_pc - see the file-level comment above for why.
bool looksLikeCpegFilename(const std::string& filename);

enum class CpegValidation {
    WellFormed,    // parsed as a valid .cpeg_pc: magic OK, own-size matches content size, record array + filename table exactly fill the content with no padding (spec Sec2-3.1's confirmed invariants)
    NotWellFormed, // magic mismatch, own-size mismatch, or the filename table didn't exactly fill the remaining content
};

struct CpegValidationResult {
    CpegValidation status = CpegValidation::WellFormed;
    std::string diagnostic; // set when status != WellFormed
};

// Runs the structural check described above on `content` (decoded
// .cpeg_pc/.cvbm_pc bytes).
CpegValidationResult validateCpegContent(const std::vector<uint8_t>& content);

// PERMANENT NO-OP. This function only ever acted on
// DecodeStatus::OkUnconfirmedContent - the status this was meant to
// corroborate or refute - and Container::decompressEntry has not produced
// that status since the container-offset fix (HANDOFF.md §9.78; see
// container.h's own DecodeStatus documentation). It is retained, unchanged
// in name and signature, purely for API compatibility with its callers
// (vpp_dump.cpp, vpp_extract.cpp): it now always returns `result`
// unchanged, for every status. It used to upgrade a WellFormed check to
// vpp::DecodeStatus::ContentValidated or downgrade a failed one (with
// `data` cleared) to vpp::DecodeStatus::ContentValidationFailed; unlike the
// .xtbl checker, this format's own validateCpegContent() carried no
// refuted structural assumption, so it is left exactly as-is as a
// standalone, independently-correct check (still exercised directly by
// synthetic_texture_test.cpp).
vpp::DecompressResult refineWithCpegValidation(vpp::DecompressResult result);

} // namespace sr3texture
