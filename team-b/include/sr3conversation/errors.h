#pragma once

#include <stdexcept>
#include <string>

namespace sr3conversation {

// Thrown for a violation of a CONFIRMED hard rule the real consumer
// enforces (spec-conversation-format.md Sec3/Sec5): the magic, the
// version, the record count being zero or above the loader's hard limit
// of 30, or the file not ending exactly at `44 + 12 * count`. Never
// thrown for anything the spec marks HYPOTHESIS/OPEN - notably never for
// the per-turn field at record +0x08, whose meaning is unresolved.
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

} // namespace sr3conversation
