// MSVC-native libFuzzer+ASan harness for vpp::Container (archive/`.vpp_pc`).
//
// This is a SEPARATE addition from fuzz/fuzz_single.cpp's own `vpp` target
// (Clang-only, CRREISH_FUZZ) - same read-only walk (entries(),
// rawEntryBytes()/decompressEntry(), openNested() for up to 64 entries), but
// kept as its own small file under fuzz/msvc/ rather than reusing that
// macro-dispatched file, so this MSVC addition never has to touch a
// pre-existing Clang-only file. See fuzz/msvc/README.md for why this exists
// alongside fuzz/fuzz.cmake and the exact flags this needs.
#include "../fuzz_common.h"
#include "vpp/container.h"

using fuzzcommon::guarded;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    vpp::ByteView v(data, size);
    guarded([&] {
        vpp::Container c(v);
        for (size_t i = 0; i < c.entries().size() && i < 64; ++i) {
            guarded([&] {
                if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
                    (void)c.rawEntryBytes(i);
                    guarded([&] {
                        vpp::Container n = c.openNested(i);
                        (void)n.entries().size();
                    });
                } else {
                    (void)c.decompressEntry(i);
                }
            });
        }
    });
    return 0;
}
