#pragma once

#include <stdexcept>
#include <string>

namespace sr3fxo {

// Thrown for a violation of a CONFIRMED hard rule (magic number). Never
// thrown for "no shaders found" or a truncated/malformed embedded shader -
// those are reported through ShaderWrapper::shaders() being incomplete
// (fewer entries than expected) rather than an exception, matching this
// project's general policy of reporting ambiguous/partial content
// gracefully instead of crashing.
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

} // namespace sr3fxo
