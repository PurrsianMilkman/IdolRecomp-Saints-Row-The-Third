// libFuzzer harness for sr3mesh::MeshBlock::parse (two buffers + offsets).
// Input layout (same as seed_capture.cpp writes):
//   u32 cLength, u32 meshOffset, u32 gSegmentOffset, u32 headerDisplacement,
//   then cLength bytes of c-content, then the rest as g-content.
#include "fuzz_common.h"
#include "sr3mesh/mesh_block.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 16) return 0;
    uint32_t cLen = fuzzcommon::u32(data);
    uint32_t meshOffset = fuzzcommon::u32(data + 4);
    uint32_t gOff = fuzzcommon::u32(data + 8);
    uint32_t disp = fuzzcommon::u32(data + 12);
    size_t rest = size - 16;
    if (cLen > rest) cLen = static_cast<uint32_t>(rest);
    vpp::ByteView c(data + 16, cLen), g(data + 16 + cLen, rest - cLen);
    fuzzcommon::guarded([&] {
        auto m = sr3mesh::MeshBlock::parse(c, meshOffset, g, gOff, disp);
        for (size_t i = 0; i < m.channels().size() && i < 16; ++i)
            fuzzcommon::guarded([&] { (void)m.decodeChannel(i); });
    });
    return 0;
}
