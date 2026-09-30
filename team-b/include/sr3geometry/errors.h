#pragma once

#include <stdexcept>
#include <string>

namespace sr3geometry {

// Thrown for a violation of a CONFIRMED hard rule (either magic number, or
// the material block's own exact-size invariant). Never thrown for a
// field the spec marks HYPOTHESIS/OPEN/UNKNOWN.
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

} // namespace sr3geometry
