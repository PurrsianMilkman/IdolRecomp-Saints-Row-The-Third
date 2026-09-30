#pragma once

#include <stdexcept>
#include <string>

namespace sr3foliage {

// Thrown for a violation of a CONFIRMED hard rule the real loader enforces
// (spec-foliage-format.md Sec3/Sec4): the material block failing to
// validate (which aborts the load for this format, unlike vehicles where
// it is optional), the outer magic/version, or the material sub-record
// chain not ending exactly where the file's own +0x28 offset says it
// should. Never thrown for anything the spec marks HYPOTHESIS/OPEN.
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

} // namespace sr3foliage
