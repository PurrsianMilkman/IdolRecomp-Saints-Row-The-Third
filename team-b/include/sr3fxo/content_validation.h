#pragma once

// Content-level corroboration for Container::decompressEntry results on
// .fxo_pc entries - the same pattern as vpp::refineWithXtblValidation
// (vpp/content_validation.h), applied to this format's own structural
// check instead of XML well-formedness. Lives in sr3fxo rather than vpp
// to keep the core container library free of per-payload-format
// knowledge; depends one-directionally on vpp_container for the shared
// DecompressResult/DecodeStatus types (vpp_container has no reciprocal
// dependency on sr3fxo).

#include <string>

#include "vpp/container.h"

namespace sr3fxo {

// True if `filename` names a .fxo_pc compiled-shader wrapper (spec-fxo-
// format.md doesn't document any alternate extension for this format,
// unlike the .xtbl/.cte_xtbl pair - a plain, case-insensitive suffix
// check is enough).
bool looksLikeFxoFilename(const std::string& filename);

enum class FxoValidation {
    WellFormed,   // parsed as a valid .fxo_pc wrapper, at least one embedded shader located via the scan-based recipe (spec §4.1), and the last shader's end token lands exactly at end-of-content (spec §3's confirmed convention)
    NotWellFormed, // magic mismatch, no shader found at all, or the last located shader doesn't end at end-of-content
};

struct FxoValidationResult {
    FxoValidation status = FxoValidation::WellFormed;
    std::string diagnostic; // set when status != WellFormed
};

// Runs the structural check described above on `content` (decoded
// .fxo_pc bytes).
FxoValidationResult validateFxoContent(const std::vector<uint8_t>& content);

// ---------------------------------------------------------------------
// Header-driven exact-consumption check (spec-fxo-format.md §6-§8, rewritten
// 2026-09-20). The count-based header size H and the 16-byte blob placement
// (see wrapper_header.h) predict the file's total length from the file's OWN
// header. validateFxoHeaderLayout() requires that prediction to equal the
// content length exactly and, optionally, that every blob starts and ends the
// way its payload family says.
//
// This is a much stronger corroborator than validateFxoContent() above: it
// compares two independently produced numbers (header-derived length vs the
// container's declared size). Measured on every real .fxo_pc /
// .fxo_pc_dx11 entry at correctly located streams: 1,691 of 1,691 pass;
// measured on the container library's own non-first-entry decodes (which
// read the wrong stream, then truncate it to the wrong file's declared
// size): 1 of 107 pass, so it rejects those where the scan-based check
// (which only looks for a token near the end) cannot.
//
// It is ADDITIVE: validateFxoContent()/refineWithFxoValidation() keep their
// old behaviour, and tests built on scan-only synthetic files still hold.
enum class FxoPayload {
    D3d9,      // .fxo_pc: vertex/pixel blobs are Direct3D 9 token streams; middle blobs are not stepped over (§8.2 (b))
    Dxbc,      // .fxo_pc_dx11: blobs are "DXBC" containers (measured 8,166/8,166; spec §8.2 calls it HIGH CONFIDENCE only); all three tables walked
    Unchecked, // layout only, all three tables walked, no per-blob check
};

// ".fxo_pc" -> D3d9, ".fxo_pc_dx11" -> Dxbc, anything else -> Unchecked.
FxoPayload fxoPayloadForFilename(const std::string& filename);

struct FxoHeaderValidationResult {
    FxoValidation status = FxoValidation::NotWellFormed;
    std::string diagnostic;   // set when status != WellFormed
    size_t headerSize = 0;    // H (spec §7.2), 0 if the header was not applicable
    size_t endOfBlobs = 0;    // running offset after the last blob, 0 if not applicable
    size_t blobCount = 0;
};

FxoHeaderValidationResult validateFxoHeaderLayout(const std::vector<uint8_t>& content,
                                                  FxoPayload payload = FxoPayload::D3d9);

// PERMANENT NO-OP, same reasoning as refineWithFxoValidation() below: it
// only ever acted on DecodeStatus::OkUnconfirmedContent, and
// Container::decompressEntry has not produced that status since the
// container-offset fix (HANDOFF.md §9.78). Retained, unchanged in name and
// signature, purely for API compatibility; now always returns `result`
// unchanged regardless of `payload`.
vpp::DecompressResult refineWithFxoHeaderValidation(vpp::DecompressResult result,
                                                    FxoPayload payload = FxoPayload::D3d9);

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
// .xtbl checker, this format's own validateFxoContent() carried no refuted
// structural assumption, so it is left exactly as-is as a standalone,
// independently-correct check (still exercised directly by
// synthetic_fxo_test.cpp).
vpp::DecompressResult refineWithFxoValidation(vpp::DecompressResult result);

} // namespace sr3fxo
