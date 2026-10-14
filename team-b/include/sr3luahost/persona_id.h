#pragma once
// Audio persona name -> persona id (spec-lua-api-behaviour.md Sec17.9, spec
// sync ce4556f FINAL): 0x0046fd00 returns 0 for an empty name or a
// case-insensitive "none"; otherwise the Wwise string-to-id hash, 32-bit
// FNV-1 (multiply, then xor) over the lower-cased name.
// Check: FNV1("pak3_interrogation") == 3989737946.
#include <cstdint>
#include <string>

namespace sr3luahost {

inline uint32_t PersonaIdFromName(const std::string& name) {
    std::string lower;
    lower.reserve(name.size());
    for (char ch : name) lower.push_back((ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch + 32) : ch);
    if (lower.empty() || lower == "none") return 0;
    uint32_t h = 2166136261u;
    for (unsigned char ch : lower) {
        h *= 16777619u;
        h ^= ch;
    }
    return h;
}

} // namespace sr3luahost
