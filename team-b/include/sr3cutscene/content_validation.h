#pragma once

// Content-level corroboration for Container::decompressEntry results on
// .csc_pc entries - the same pattern as the other format corroborators in
// this project (xtbl, fxo, cpeg, cfmesh, ctdg). Lives in sr3cutscene
// rather than vpp to keep the core container library free of
// per-payload-format knowledge; depends one-directionally on vpp_container
// for the shared DecompressResult/DecodeStatus types.
//
// Strength of the check, stated honestly: this format has NO magic number -
// only a 2-byte version that must equal 4 (spec-cutscene-camera-format.md
// Sec3). On its own that is weak evidence. What carries the check is the
// exact-size identity `record_offset + count * 0x68 == size` plus the
// per-channel bounds walk: CameraScript::parse() reads every channel
// pointer in every shot and requires each time/value array to lie inside
// the file. A corrupted decode that still satisfies all of that is a
// narrow target. Weaker than .ctdg_pc's (magic + version + a total-size
// identity with no slack anywhere), stronger than a bare header sniff.
//
// Only .csc_pc is matched - spec Sec2 confirms 0 .gsc_pc files exist and
// the registration declares no secondary extension.

#include <string>

#include "vpp/container.h"

namespace sr3cutscene {

// True if `filename` names a .csc_pc cutscene camera script
// (case-insensitive exact extension match).
bool looksLikeCscFilename(const std::string& filename);

enum class CscValidation {
    WellFormed,    // CameraScript::parse() succeeded: version 4, a non-null record-array offset, the array ending exactly at EOF, and every channel's arrays in bounds
    NotWellFormed, // CameraScript::parse() threw FormatError - see the diagnostic for which check failed
};

struct CscValidationResult {
    CscValidation status = CscValidation::WellFormed;
    std::string diagnostic; // set when status != WellFormed
};

// Runs CameraScript::parse() on `content` (decoded .csc_pc bytes) and
// reports whether it succeeded.
CscValidationResult validateCscContent(const std::vector<uint8_t>& content);

// PERMANENT NO-OP. This function only ever acted on
// DecodeStatus::OkUnconfirmedContent - the status this was meant to
// corroborate or refute - and Container::decompressEntry has not produced
// that status since the container-offset fix (HANDOFF.md §9.78; see
// container.h's own DecodeStatus documentation). It is retained, unchanged
// in name and signature, purely for API compatibility: it now always
// returns `result` unchanged, for every status. It used to upgrade a
// WellFormed check to vpp::DecodeStatus::ContentValidated or downgrade a
// failed one (with `data` cleared) to vpp::DecodeStatus::ContentValidationFailed;
// unlike the .xtbl checker, this format's own validateCscContent() carried
// no refuted structural assumption, so it is left exactly as-is as a
// standalone, independently-correct check (still exercised directly by
// synthetic_csc_test.cpp).
vpp::DecompressResult refineWithCscValidation(vpp::DecompressResult result);

} // namespace sr3cutscene
