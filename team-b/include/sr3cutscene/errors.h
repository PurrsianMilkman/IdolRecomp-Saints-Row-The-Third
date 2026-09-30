#pragma once

#include <stdexcept>
#include <string>

namespace sr3cutscene {

// Thrown for a violation of a CONFIRMED hard rule the real parser
// enforces (spec-cutscene-camera-format.md Sec3/Sec6): the version not
// being 4, the record-array offset being null (-1), the record array not
// ending exactly at EOF, or a channel's key/time/value arrays running
// past the end of the file. Never thrown for anything the spec marks
// HYPOTHESIS/OPEN - notably never for the still-open conventions of Sec10.9
// (orientation axes/handedness, FOV axis, the roles of the four DOF
// breakpoints). The decode/sample functions also throw it, for asking a
// channel for a value width it does not have (e.g. decodeFloats on channel 1).
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

} // namespace sr3cutscene
