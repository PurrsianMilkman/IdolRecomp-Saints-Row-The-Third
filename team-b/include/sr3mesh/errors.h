#pragma once

#include <stdexcept>
#include <string>

namespace sr3mesh {

// Thrown for a violation of a CONFIRMED hard rule (spec-vertex-format.md
// Sec4/Sec9): the version field not reading 9, the g-segment walk not
// landing exactly on the declared g-length, either check-value bookend
// failing, or a channel running past the end of its buffer.
//
// Spec Sec9 step 8 states the contract this reader adopts verbatim: "the
// walk must consume exactly the declared g-length and end on a repeat of
// the check value. If it does not, stop - do not guess."
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

} // namespace sr3mesh
