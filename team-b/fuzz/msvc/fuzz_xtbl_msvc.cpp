// MSVC-native libFuzzer+ASan harness for sr3xtbl::ParseDocument (`.xtbl` /
// `.cte_xtbl` gameplay tables) - the tolerant hand-rolled XML parser
// spec-xtbl-format.md documents as needing to survive real shipped quirks
// (mismatched close tags, a stray 0x1F control byte in text, an element
// name with a space in it), which makes it a high-value fuzz target: every
// per-table typed reader in sr3tables_* builds on top of this. See
// fuzz/msvc/README.md.
#include "../fuzz_common.h"
#include "sr3xtbl/xtbl.h"

#include <string_view>

using fuzzcommon::guarded;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    guarded([&] { (void)sr3xtbl::ParseDocument(data, size); });
    guarded([&] {
        sr3xtbl::ParseOptions o;
        o.decodeEntities = false;
        (void)sr3xtbl::ParseDocument(std::string_view(reinterpret_cast<const char*>(data), size), o);
    });
    return 0;
}
