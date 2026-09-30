#include "vpp/hash.h"

namespace vpp {

namespace {

inline uint32_t rotl32(uint32_t value, unsigned bits) {
    bits &= 31u;
    return bits == 0 ? value : (value << bits) | (value >> (32u - bits));
}

} // namespace

uint32_t hashFilename(std::string_view name) {
    uint32_t hash = 0;
    for (unsigned char c : name) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<unsigned char>(c - 'A' + 'a');
        }
        hash = rotl32(hash, 6);
        hash ^= c;
    }
    return hash;
}

} // namespace vpp
