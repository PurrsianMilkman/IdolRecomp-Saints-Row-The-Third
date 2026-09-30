#pragma once

// Content-level corroboration for Container::decompressEntry results on
// .ctdg_pc entries - the same pattern as vpp::refineWithXtblValidation,
// sr3fxo::refineWithFxoValidation, sr3texture::refineWithCpegValidation and
// sr3foliage::refineWithCfmeshValidation. Lives in sr3conversation rather
// than vpp to keep the core container library free of per-payload-format
// knowledge; depends one-directionally on vpp_container for the shared
// DecompressResult/DecodeStatus types.
//
// This format makes an unusually strong corroborator, for a reason worth
// stating: it has a magic AND a version AND an exact-size identity
// (`44 + 12 * count == size`, spec-conversation-format.md Sec1) that
// accounts for every byte of the file, with no offsets or pointer fixups
// anywhere to introduce slack. A truncated or corrupted decode therefore
// has essentially nowhere to hide - it must either break the magic/version
// or fail the size identity. Compare sr3texture's, which has to reason
// about a filename table exactly filling the remainder.
//
// Only .ctdg_pc is matched - spec Sec2 confirms the format registers no
// secondary (g-side) extension, so unlike sr3texture there is no paired
// file to consider.

#include <string>

#include "vpp/container.h"

namespace sr3conversation {

// True if `filename` names a .ctdg_pc mission conversation
// (case-insensitive exact extension match).
bool looksLikeCtdgFilename(const std::string& filename);

enum class CtdgValidation {
    WellFormed,    // Conversation::parse() succeeded: magic + version matched, the record count was in 1..30, and the file ended exactly at 44 + 12 x count
    NotWellFormed, // Conversation::parse() threw FormatError - see the diagnostic for which check failed
};

struct CtdgValidationResult {
    CtdgValidation status = CtdgValidation::WellFormed;
    std::string diagnostic; // set when status != WellFormed
};

// Runs Conversation::parse() on `content` (decoded .ctdg_pc bytes) and
// reports whether it succeeded.
CtdgValidationResult validateCtdgContent(const std::vector<uint8_t>& content);

// PERMANENT NO-OP. This function only ever acted on
// DecodeStatus::OkUnconfirmedContent - the status this was meant to
// corroborate or refute - and Container::decompressEntry has not produced
// that status since the container-offset fix (HANDOFF.md §9.78; see
// container.h's own DecodeStatus documentation). It is retained, unchanged
// in name and signature, purely for API compatibility: it now always
// returns `result` unchanged, for every status. It used to upgrade a
// WellFormed check to vpp::DecodeStatus::ContentValidated or downgrade a
// failed one (with `data` cleared) to vpp::DecodeStatus::ContentValidationFailed;
// unlike the .xtbl checker, this format's own validateCtdgContent() carried
// no refuted structural assumption, so it is left exactly as-is as a
// standalone, independently-correct check (still exercised directly by
// synthetic_ctdg_test.cpp).
vpp::DecompressResult refineWithCtdgValidation(vpp::DecompressResult result);

} // namespace sr3conversation
