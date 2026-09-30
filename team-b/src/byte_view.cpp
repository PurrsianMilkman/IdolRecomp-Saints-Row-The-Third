#include "vpp/byte_view.h"

namespace vpp {

uint8_t ByteView::at(size_t offset) const {
    if (offset >= size_) {
        throw std::out_of_range("ByteView::at: offset out of range");
    }
    return data_[offset];
}

uint16_t ByteView::readU16LE(size_t offset) const {
    if (offset + 2 < offset || offset + 2 > size_) {
        throw std::out_of_range("ByteView::readU16LE: offset out of range");
    }
    return static_cast<uint16_t>(static_cast<uint32_t>(data_[offset]) |
                                  (static_cast<uint32_t>(data_[offset + 1]) << 8));
}

uint32_t ByteView::readU32LE(size_t offset) const {
    // offset + 4 < offset would indicate size_t overflow on the addition.
    if (offset + 4 < offset || offset + 4 > size_) {
        throw std::out_of_range("ByteView::readU32LE: offset out of range");
    }
    return static_cast<uint32_t>(data_[offset]) |
           (static_cast<uint32_t>(data_[offset + 1]) << 8) |
           (static_cast<uint32_t>(data_[offset + 2]) << 16) |
           (static_cast<uint32_t>(data_[offset + 3]) << 24);
}

ByteView ByteView::subview(size_t offset, size_t length) const {
    if (offset > size_ || length > size_ - offset) {
        throw std::out_of_range("ByteView::subview: range out of bounds");
    }
    return ByteView(data_ + offset, length);
}

size_t ByteView::cStringLength(size_t offset) const {
    if (offset > size_) {
        throw std::out_of_range("ByteView::cStringLength: offset out of range");
    }
    for (size_t i = offset; i < size_; ++i) {
        if (data_[i] == 0) {
            return i - offset;
        }
    }
    throw std::out_of_range(
        "ByteView::cStringLength: no NUL terminator found before end of view");
}

} // namespace vpp
