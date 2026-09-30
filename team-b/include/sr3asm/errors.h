#pragma once

#include <stdexcept>
#include <string>

namespace sr3asm {

// Thrown when the bytes cannot be a version-11 .asm_pc manifest under
// spec-asm-format.md sections 6-10: wrong magic, unsupported version, or a
// read (header, table, record, entry) that would run past the end of the
// buffer, or a negative entry_count. The message says which.
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

} // namespace sr3asm
