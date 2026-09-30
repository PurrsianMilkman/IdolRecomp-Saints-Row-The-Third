#pragma once

// Content-level corroboration for Container::decompressEntry results.
//
// decompressEntry's own checks (no zlib error, length matches +0x0C) are
// necessary but NOT sufficient to prove decoded content is actually
// correct - see DecodeStatus::OkUnconfirmedContent's own documentation
// (container.h) for why: Team A found that non-first entries in
// multi-entry mode-(a) containers can pass both checks while silently
// corrupted. spec-xtbl-format.md §1 independently reproduces and sharpens
// this exact finding for .xtbl-family content specifically, with a
// concrete corruption signature: the corrupted output is NOT well-formed
// XML (truncated mid-tag, missing closing elements), despite matching its
// declared byte count exactly.
//
// Where a format-specific structural check exists, running it on an
// OkUnconfirmedContent result gives real corroborating evidence one way
// or the other, instead of leaving the result merely "unconfirmed, no
// check available." This header provides exactly that check for the
// .xtbl family.
//
// Deliberately NOT wired into Container::decompressEntry itself - that
// stays a generic, payload-format-agnostic parser with no knowledge of
// .xtbl or XML. Callers that know (from the entry's filename) that a
// decoded entry should be .xtbl-family content opt into this refinement
// themselves, e.g.:
//
//   DecompressResult r = container.decompressEntry(i);
//   if (looksLikeXtblFilename(container.entries()[i].name)) {
//       r = refineWithXtblValidation(std::move(r));
//   }

#include <cstdint>
#include <string>
#include <vector>

#include "vpp/container.h"

namespace vpp {

// True if `filename` plausibly names an .xtbl-family gameplay data table.
// spec-xtbl-format.md §4 confirms BOTH ".xtbl" and ".cte_xtbl" are real,
// distinct extensions in this family, and explicitly warns that matching
// only the exact ".xtbl" suffix will silently skip the ".cte_xtbl"
// category - so this matches broadly on "filename ends with xtbl"
// (case-insensitive), per the spec's own recommendation, rather than only
// the bare ".xtbl" suffix.
bool looksLikeXtblFilename(const std::string& filename);

// Outcome of structurally validating decoded .xtbl-family content: does it
// consist of well-formed, properly-nested XML tags? This is NOT a
// general-purpose validating XML parser - see content_validation.cpp's
// top-of-file comment for exactly what it does and doesn't check. It is
// purpose-built to detect the specific corruption signature
// spec-xtbl-format.md §1 documents: truncated mid-tag, missing closing
// elements.
//
// This type used to also carry a WrongRootShape variant for content whose
// outermost element wasn't <root> directly containing a <Table> child
// (spec §2's convention at the time). That check was removed, not merely
// disabled: HANDOFF.md §9.79 measured 260 of 2,222 real shipped
// .xtbl-family files (blend_tree files, state_machine files,
// action_nodes.xtbl, node_graph_files.xtbl, and others) that legitimately
// have no <Table> child at all, so it would have produced false positives
// on real, valid data even on the rare call where it still ran. See
// refineWithXtblValidation's comment below for why it stopped running in
// the first place.
enum class XtblValidation {
    WellFormed,       // passed the well-formedness check below
    NotWellFormedXml, // tag nesting/balance failed, or content is truncated mid-tag / has unclosed elements - the exact corruption signature spec §1 describes
};

struct XtblValidationResult {
    XtblValidation status = XtblValidation::WellFormed;
    std::string diagnostic; // set when status != WellFormed
};

// Runs the well-formedness check described above on `content` (decoded
// .xtbl-family bytes).
XtblValidationResult validateXtblContent(const std::vector<uint8_t>& content);

// PERMANENT NO-OP. This function only ever acted on
// DecodeStatus::OkUnconfirmedContent, and Container::decompressEntry has
// not produced that status since the container-offset fix (HANDOFF.md
// §9.78 - see container.h's own DecodeStatus documentation). It is
// retained, unchanged in name and signature, purely for API compatibility
// with its several callers that already invoke it unconditionally
// (vpp_dump.cpp, vpp_extract.cpp,
// tools/validation/validate_container_decode.cpp): it now always returns
// `result` unchanged, for every status.
//
// It used to upgrade a WellFormed check to DecodeStatus::ContentValidated
// and a failed one - which, before HANDOFF.md §9.79, also included the
// now-removed WrongRootShape case - to DecodeStatus::ContentValidationFailed.
// That WrongRootShape check was itself wrong even when it did run (see the
// XtblValidation enum's comment above), so retiring this wrapper is a
// correctness fix as well as a dead-code removal, not just a formality.
DecompressResult refineWithXtblValidation(DecompressResult result);

} // namespace vpp
