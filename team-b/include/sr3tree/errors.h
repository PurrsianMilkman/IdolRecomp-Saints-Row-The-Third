#pragma once

#include <stdexcept>
#include <string>

namespace sr3tree {

// Thrown for a violation of a CONFIRMED hard rule the real loader enforces
// (spec-tree-format.md Sec3/Sec4/Sec6): the shared material-reference block
// failing to validate, the 'TREE' block's magic/version, a material
// sub-record chain overrunning content, or a Mesh sub-block failing to
// parse. Never thrown for anything the spec marks HYPOTHESIS/OPEN.
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

} // namespace sr3tree
