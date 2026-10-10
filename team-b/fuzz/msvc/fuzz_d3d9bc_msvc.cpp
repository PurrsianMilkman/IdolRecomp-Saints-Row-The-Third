// MSVC-native libFuzzer+ASan harness for the D3D9 SM2/SM3 shader bytecode
// disassembler (sr3d3d9bc::disassemble / readConstantTable /
// translateToHlsl, spec-d3d9-sm2-sm3-bytecode.md) - mirrors the `d3d9bc`
// branch of fuzz/fuzz_single.cpp (Clang-only), kept as its own file so this
// MSVC addition never has to touch that pre-existing file. See
// fuzz/msvc/README.md.
#include "../fuzz_common.h"
#include "sr3d3d9bc/ctab.h"
#include "sr3d3d9bc/disassembler.h"
#include "sr3d3d9bc/hlsl_translator.h"

using fuzzcommon::guarded;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    vpp::ByteView v(data, size);
    guarded([&] {
        auto d = sr3d3d9bc::disassemble(v);
        guarded([&] {
            auto ct = sr3d3d9bc::readConstantTable(v, d);
            guarded([&] { (void)sr3d3d9bc::translateToHlsl(d, ct, v, sr3d3d9bc::HlslTarget::SM3Legacy); });
            guarded([&] { (void)sr3d3d9bc::translateToHlsl(d, ct, v, sr3d3d9bc::HlslTarget::SM4_5); });
        });
    });
    return 0;
}
