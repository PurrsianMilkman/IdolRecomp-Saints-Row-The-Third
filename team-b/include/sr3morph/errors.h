#pragma once

#include <stdexcept>
#include <string>

namespace sr3morph {

// Thrown for a violation of a CONFIRMED hard rule the real loader itself
// enforces (spec-morph-format.md Sec3): either magic, either version, an
// out-of-range mode, or a structural walk that doesn't land exactly on the
// end of the file with the trailing sentinel in place (Sec4). Never thrown
// for anything the spec marks HYPOTHESIS/OPEN/UNKNOWN.
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

} // namespace sr3morph
