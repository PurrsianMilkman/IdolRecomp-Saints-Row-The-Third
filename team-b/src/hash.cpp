#include "vpp/hash.h"

namespace vpp {

namespace {

inline uint32_t rotl32(uint32_t value, unsigned bits) {
    bits &= 31u;
    return bits == 0 ? value : (value << bits) | (value >> (32u - bits));
}

} // namespace

uint32_t hashFilename(std::string_view name) {
    // spec-differential-results D-5: each byte is read as a SIGNED 8-bit value widened to 32 bits (0x80 becomes
    // 0xFFFFFF80) before the A-Z lower-casing test and the XOR. ASCII names are unaffected.
    uint32_t hash = 0;
    for (char ch : name) {
        int32_t c = static_cast<int32_t>(static_cast<int8_t>(ch));
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        hash = rotl32(hash, 6);
        hash ^= static_cast<uint32_t>(c);
    }
    return hash;
}

} // namespace vpp
