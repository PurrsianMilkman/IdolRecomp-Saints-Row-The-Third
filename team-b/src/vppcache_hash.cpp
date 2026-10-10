#include "vppcache/hash.h"

#include <cstdio>

namespace vppcache {

std::string toHex16(uint64_t value) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(value));
    return std::string(buf, 16);
}

} // namespace vppcache
