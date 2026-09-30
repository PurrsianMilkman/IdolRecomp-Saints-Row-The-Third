#pragma once

#include <stdexcept>
#include <string>

namespace sr3rig {

// Thrown for a violation of a rule the spec confirms across all 585
// shipped rigs (spec-rig-format.md §3/§4): a file too small for its
// declared counts, a bone name offset that does not resolve, or parent
// indices that do not form a valid tree. Never thrown for anything the
// spec marks HYPOTHESIS/OPEN - notably not for the rotation triple, whose
// convention is unresolved and which is surfaced raw.
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

} // namespace sr3rig
