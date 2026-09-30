#pragma once

// Content-level corroboration for Container::decompressEntry results on
// .cfmesh_pc entries - the same pattern as vpp::refineWithXtblValidation,
// sr3fxo::refineWithFxoValidation, and sr3texture::refineWithCpegValidation,
// applied to this format's own structural check. Lives in sr3foliage rather
// than vpp to keep the core container library free of per-payload-format
// knowledge; depends one-directionally on vpp_container for the shared
// DecompressResult/DecodeStatus types (and on sr3geometry, transitively via
// foliage_mesh.h, for the shared MaterialBlock).
//
// The structural check here is unusually strong for this project: FoliageMesh::
// parse() doesn't just check a magic and a couple of lengths - it requires the
// material sub-record chain to walk forward and land EXACTLY on the file's own
// +0x28 texture-name-table offset (see foliage_mesh.h/.cpp), which spec-foliage-
// format.md §3/§9 documents as holding 19/19 across the entire real population.
// A parse() success is therefore already strong corroborating evidence on its
// own (not merely "no exception was thrown on some loosely-checked shape"), so
// this corroborator can just be "did parse() succeed" rather than needing a
// second, independent structural pass the way e.g. the .xtbl checker does.
//
// Only .cfmesh_pc is matched - spec-foliage-format.md §2 confirms the format
// is registered with NO secondary (g-side) extension, and the entire 19-file
// population has 0 .gfmesh_pc entries, so there is no paired-file case to
// consider here the way sr3texture has for .gpeg_pc/.gvbm_pc.

#include <string>

#include "vpp/container.h"

namespace sr3foliage {

// True if `filename` names a .cfmesh_pc foliage mesh (case-insensitive exact
// extension match - spec-foliage-format.md §2 confirms this is the one and
// only shipped extension for this format, not a prefix heuristic).
bool looksLikeCfmeshFilename(const std::string& filename);

enum class CfmeshValidation {
    WellFormed,    // FoliageMesh::parse() succeeded: material block validated, outer magic/version matched, and the material sub-record chain landed exactly on the file's own +0x28 target (spec §3/§9's whole-population invariant, re-run per file)
    NotWellFormed, // FoliageMesh::parse() threw FormatError - see the diagnostic for which check failed
};

struct CfmeshValidationResult {
    CfmeshValidation status = CfmeshValidation::WellFormed;
    std::string diagnostic; // set when status != WellFormed
};

// Runs FoliageMesh::parse() on `content` (decoded .cfmesh_pc bytes) and
// reports whether it succeeded. See the file-level comment for why a
// successful parse() is itself the structural check here, rather than a
// second independent pass.
CfmeshValidationResult validateCfmeshContent(const std::vector<uint8_t>& content);

// PERMANENT NO-OP. This function only ever acted on
// DecodeStatus::OkUnconfirmedContent - the status this was meant to
// corroborate or refute - and Container::decompressEntry has not produced
// that status since the container-offset fix (HANDOFF.md §9.78; see
// container.h's own DecodeStatus documentation). It is retained, unchanged
// in name and signature, purely for API compatibility: it now always
// returns `result` unchanged, for every status. It used to upgrade a
// WellFormed check to vpp::DecodeStatus::ContentValidated or downgrade a
// failed one (with `data` cleared) to vpp::DecodeStatus::ContentValidationFailed;
// unlike the .xtbl checker, this format's own validateCfmeshContent()
// carried no refuted structural assumption, so it is left exactly as-is as
// a standalone, independently-correct check (still exercised directly by
// synthetic_foliage_test.cpp).
vpp::DecompressResult refineWithCfmeshValidation(vpp::DecompressResult result);

} // namespace sr3foliage
