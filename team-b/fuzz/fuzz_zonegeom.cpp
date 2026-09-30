// libFuzzer harness for sr3zone::ZoneGeometry::locate (two buffers).
// Input layout: u32 cznLength, then cznLength bytes, then the gzn bytes.
#include "fuzz_common.h"
#include "sr3zone/zone_geometry.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 4) return 0;
    uint32_t cLen = fuzzcommon::u32(data);
    size_t rest = size - 4;
    if (cLen > rest) cLen = static_cast<uint32_t>(rest);
    vpp::ByteView czn(data + 4, cLen), gzn(data + 4 + cLen, rest - cLen);
    fuzzcommon::guarded([&] { (void)sr3zone::ZoneGeometry::locate(czn, gzn); });
    return 0;
}
