#pragma once

#include <stdexcept>
#include <string>

namespace sr3texture {

// Thrown for a violation of a CONFIRMED hard rule (magic number, or one of
// the exact-size invariants spec-texture-format.md confirmed hold on every
// real sample checked: own-size matching the actual file size, the record
// array + filename table exactly filling the file with no padding). Never
// thrown for anything the spec marks HYPOTHESIS/OPEN/UNKNOWN.
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

} // namespace sr3texture
