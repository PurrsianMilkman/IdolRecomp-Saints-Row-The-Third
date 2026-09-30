#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace vpp {

// Non-owning view over a contiguous byte range. All accessors are bounds
// checked and throw std::out_of_range rather than reading past the end of
// the buffer, so a malformed or truncated archive fails loudly instead of
// silently reading garbage / corrupting memory.
class ByteView {
public:
    ByteView() = default;
    ByteView(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    uint8_t at(size_t offset) const;

    // Reads a little-endian uint16 at `offset`.
    uint16_t readU16LE(size_t offset) const;

    // Reads a little-endian uint32 at `offset`. All multi-byte integer
    // fields in the container format are little-endian (spec §1).
    uint32_t readU32LE(size_t offset) const;

    // Returns a sub-view [offset, offset + length). Throws std::out_of_range
    // if that range does not fit entirely within this view.
    ByteView subview(size_t offset, size_t length) const;

    // Length (not including the terminator) of a NUL-terminated string
    // starting at `offset`. Throws std::out_of_range if no NUL byte is
    // found before the end of the view.
    size_t cStringLength(size_t offset) const;

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
};

} // namespace vpp
