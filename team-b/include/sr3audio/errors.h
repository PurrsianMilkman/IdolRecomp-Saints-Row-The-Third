#pragma once

#include <stdexcept>
#include <string>

namespace sr3audio {

// Thrown for a violation of a CONFIRMED hard rule of the `_media.bnk_pc`
// wrapper (spec-audio-format.md Sec4) or of the plain `.bnk_pc` sibling's
// public 16-byte `BKHD` prefix (Sec3): a bad magic, a header too short to
// contain its own CONFIRMED fields, a record table that runs off the end of
// the supplied bytes, or a block-chain walk that breaks, stalls, or fails to
// land exactly on the caller-supplied total entry size.
//
// NEVER thrown on account of a field either spec marks OPEN - the `+0x08`
// constant, `+0x14`, `+0x1C` and the per-record `tag` are surfaced raw and
// uninterpreted (see media_bank.h), exactly as sr3zone/sr3morph do for their
// own unresolved fields. In particular the header's `+0x18` record-count
// field is NOT enforced against the walked count: spec-audio-format.md Sec11
// records that this project's own prior pass had two rival "count"-looking
// fields and that the outer container's declared size is the better
// terminating oracle - so the count is exposed for the caller to compare
// against, not used to gate the parse.
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

} // namespace sr3audio
