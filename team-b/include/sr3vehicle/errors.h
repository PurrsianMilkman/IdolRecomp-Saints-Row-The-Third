#pragma once

#include <stdexcept>
#include <string>

namespace sr3vehicle {

// Thrown for a violation of a rule `spec-vehicle-geometry.md` confirms
// across all 393 shipped vehicles: the `0x38` header word, a part count or
// part array that does not fit the file, a part name pointer that does not
// resolve, or a parent index that is neither -1 nor less than the count.
//
// Never thrown for anything the spec marks HYPOTHESIS or OPEN - notably not
// for unrecognised part-type enum values (types 5, 7, 9-13, 15, 16, 19-21
// and 26 are too rare to read confidently and are surfaced raw), and not
// for the many header offsets whose targets are uncharacterised.
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

} // namespace sr3vehicle
