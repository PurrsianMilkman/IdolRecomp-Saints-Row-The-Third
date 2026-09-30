#pragma once

#include <stdexcept>
#include <string>

namespace sr3save {

// Thrown by both save_directory.h and save_snapshot.h for a violation of
// a CONFIRMED hard rule (exact file size, an invariant the real loader
// itself enforces). Never thrown for anything spec-save-format.md marks
// HYPOTHESIS/OPEN - those fields are simply exposed as raw, uninterpreted
// bytes/values instead.
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

} // namespace sr3save
