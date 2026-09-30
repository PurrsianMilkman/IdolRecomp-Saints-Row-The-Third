#pragma once

#include <stdexcept>
#include <string>

namespace sr3anim {

// Thrown for a violation of a CONFIRMED hard rule - the `ANIM` magic, or
// the version field not being exactly 14. Both are rules the real loader
// itself enforces (spec-anim-format.md Sec2), not inferences. Never thrown
// for anything the spec marks HYPOTHESIS/OPEN/UNKNOWN, and never for a
// file that is simply short: the 12 real sub-0x48-byte files (spec Sec8
// item 6) are valid, and Animation::parse handles them.
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

} // namespace sr3anim
