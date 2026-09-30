#pragma once

#include <stdexcept>
#include <string>

namespace sr3vintdoc {

// Thrown for a violation of a CONFIRMED hard rule (spec-vint-doc-format.md
// Sec2/Sec3/Sec4): the `0x00003027` magic not being at `+0x00` (callers
// should check magic themselves via looksLikeVintDoc()/tryParse() first and
// route a non-match to a clean "not a binary vint_doc" result rather than
// ever reaching this throw), or the content being too short to hold the
// fixed 30-byte header, or a structural walk running off the end of the
// buffer while reading a count-driven array whose count itself was read
// from the file.
//
// NEVER thrown for a version outside the CONFIRMED-shipped {1, 2} set, a
// property tag outside the CONFIRMED-shipped 1-7 range, or anything else
// either spec marks OPEN - those are DISAGREEMENTS to surface through the
// population validator (tools/validation/validate_vintdoc_population.cpp),
// not parse failures. A version/tag outside the documented set is still
// read structurally where the record shape makes that possible (see
// vint_doc.h's own notes), so one out-of-range value doesn't necessarily
// abort the whole file.
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

} // namespace sr3vintdoc
