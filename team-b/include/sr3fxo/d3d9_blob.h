#pragma once

// Minimal reader for the PUBLIC Direct3D 9 compiled-shader token stream
// (spec-fxo-format.md §3: version token, "CTAB" constant-table comment, end
// token). Everything here is Microsoft's documented format, not anything
// proprietary to the game; it exists so a harness can check the wrapper
// header's constant tables (spec §7.3) against the bytecode's own constant
// table without an external disassembler.
//
//   token 0            version: 0xFFFE#### vertex, 0xFFFF#### pixel
//   then comment tokens: low 16 bits 0xFFFE, size in dwords in bits 16..30;
//   a comment whose payload starts with the FourCC "CTAB" holds
//     D3DXSHADER_CONSTANTTABLE {Size, Creator, Version, Constants,
//                               ConstantInfo, Flags, Target}
//   and Constants entries of D3DXSHADER_CONSTANTINFO (20 bytes):
//     {Name offset, RegisterSet u16, RegisterIndex u16, RegisterCount u16,
//      Reserved u16, TypeInfo offset, DefaultValue offset}
//   with every offset relative to the start of the table struct (the dword
//   after the FourCC).
//   last dword: end token 0x0000FFFF.

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "vpp/byte_view.h"

namespace sr3fxo {

struct D3d9Constant {
    std::string name;
    uint16_t registerSet = 0;
    uint16_t registerIndex = 0;
    uint16_t registerCount = 0;
};

struct D3d9BlobInfo {
    bool versionTokenValid = false; // 0xFFFE#### or 0xFFFF#### with major 1..3
    bool isVertex = false;
    uint8_t major = 0, minor = 0;
    bool endTokenAtLastDword = false; // last 4 bytes == 0x0000FFFF
    bool ctabFound = false;
    std::vector<D3d9Constant> constants;
};

inline D3d9BlobInfo inspectD3d9Blob(vpp::ByteView blob) {
    D3d9BlobInfo info;
    if (blob.size() < 8) return info;
    const uint32_t v = blob.readU32LE(0);
    const uint32_t hi = v & 0xFFFF0000u;
    const uint8_t minor = static_cast<uint8_t>(v & 0xFF);
    const uint8_t major = static_cast<uint8_t>((v >> 8) & 0xFF);
    if ((hi == 0xFFFE0000u || hi == 0xFFFF0000u) && major >= 1 && major <= 3) {
        info.versionTokenValid = true;
        info.isVertex = hi == 0xFFFE0000u;
        info.major = major;
        info.minor = minor;
    }
    info.endTokenAtLastDword = blob.readU32LE(blob.size() - 4) == 0x0000FFFFu;
    if (!info.versionTokenValid) return info;

    // Walk the leading run of comment tokens.
    size_t pos = 4;
    while (pos + 4 <= blob.size()) {
        const uint32_t tok = blob.readU32LE(pos);
        if ((tok & 0xFFFFu) != 0xFFFEu) break;
        const size_t payloadDwords = (tok >> 16) & 0x7FFFu;
        const size_t payload = pos + 4;
        const size_t payloadEnd = payload + payloadDwords * 4;
        if (payloadEnd > blob.size()) break;
        if (payloadDwords >= 8 && blob.readU32LE(payload) == 0x42415443u /* "CTAB" */) {
            const size_t t = payload + 4; // table struct start; offsets are relative to it
            if (t + 28 <= payloadEnd) {
                const uint32_t n = blob.readU32LE(t + 12);
                const uint32_t infoOff = blob.readU32LE(t + 16);
                info.ctabFound = true;
                for (uint32_t i = 0; i < n; ++i) {
                    const size_t e = t + infoOff + static_cast<size_t>(i) * 20;
                    if (e + 20 > payloadEnd) break;
                    D3d9Constant c;
                    const size_t nameOff = t + blob.readU32LE(e);
                    if (nameOff >= payloadEnd) continue;
                    for (size_t k = nameOff; k < payloadEnd && blob.data()[k] != 0; ++k)
                        c.name.push_back(static_cast<char>(blob.data()[k]));
                    c.registerSet = blob.readU16LE(e + 4);
                    c.registerIndex = blob.readU16LE(e + 6);
                    c.registerCount = blob.readU16LE(e + 8);
                    info.constants.push_back(std::move(c));
                }
            }
            break;
        }
        pos = payloadEnd;
    }
    return info;
}

} // namespace sr3fxo
