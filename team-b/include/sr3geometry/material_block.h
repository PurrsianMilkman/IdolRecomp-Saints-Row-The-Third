// Reader for the shared "material reference block" (spec-texture-format.md
// Appendix, corrected/generalized by spec-geometry-format.md Sec3.1): a
// fixed 32-byte header plus a texture-name table, confirmed (spec-geometry-
// format.md Sec3.1) to be embedded verbatim at the START of `.matlib_pc`,
// `.ccmesh_pc`, `.cefct_pc`, and `.clmesh_pc` alike - a general-purpose,
// format-agnostic block, not specific to any one of those file types.
// `.ccmesh_pc`'s own geometry data (sr3geometry::GeometryBlock) always
// follows immediately after one of these, so this is the first thing any
// `.ccmesh_pc` reader parses.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sr3geometry/errors.h"
#include "vpp/byte_view.h"

namespace sr3geometry {

using vpp::ByteView;

constexpr uint32_t kMaterialBlockMagic = 0x00043854u; // CONFIRMED (spec-texture-format.md Appendix)
constexpr size_t kMaterialBlockHeaderSize = 0x20;      // 32 bytes, CONFIRMED

// Rounds `value` up to the next multiple of 16 - the CONFIRMED alignment
// pad used both right after this block (before the geometry block's own
// magic - spec-geometry-format.md Sec4.1) and between every array inside
// the geometry block (see geometry_block.h).
constexpr size_t roundUp16(size_t value) {
    return (value + 15) / 16 * 16;
}

struct MaterialBlock {
    uint32_t textureSlotCount = 0;         // +0x0C, CONFIRMED
    std::vector<std::string> textureNames; // CONFIRMED: textureSlotCount null-terminated names, back-to-back, starting at +0x20

    // Total on-disk size of this block (header + name table), CONFIRMED
    // via the block's own +0x04 length field (spec: "equals
    // name_table_end - 0x20 to the byte") - the byte offset where content
    // immediately following this block (the geometry block's 16-byte
    // alignment pad, for `.ccmesh_pc`) begins.
    size_t totalSize = 0;

    // Parses a material block starting at byte 0 of `bytes` (a subview,
    // not necessarily the whole file - callers pass the full .ccmesh_pc/
    // .matlib_pc/.cefct_pc/.clmesh_pc content, since this block always
    // starts at that content's own offset 0). Throws FormatError if the
    // magic doesn't match, or if the name table doesn't exactly match the
    // declared +0x04 length (a real, CONFIRMED invariant - not merely
    // trusting the length field, this cross-checks it against the actual
    // null-terminated name bytes).
    static MaterialBlock parse(ByteView bytes);
};

} // namespace sr3geometry
