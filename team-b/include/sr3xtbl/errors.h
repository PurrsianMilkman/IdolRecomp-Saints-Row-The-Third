#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

namespace sr3xtbl {

// Thrown by the XML parser (xtbl.h) for input that is not XML-like at all:
// no root element, or the input ends in the middle of a tag / comment /
// CDATA section / processing instruction, or it is UTF-16. It is NOT thrown
// for the tolerated authoring defects the real data ships with (a mismatched
// close tag, an illegal control character in text, an element name containing
// a space, ...): those are recovered from and recorded in Document::warnings().
// offset() is the byte offset into the input at which the problem was
// detected (or, for "no root", the input size).
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what, size_t offset = 0)
        : std::runtime_error(what), offset_(offset) {}
    size_t offset() const { return offset_; }

private:
    size_t offset_;
};

} // namespace sr3xtbl
