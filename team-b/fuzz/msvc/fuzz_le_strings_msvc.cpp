// MSVC-native libFuzzer+ASan harness for sr3locstr::parseLeStrings
// (`.le_strings` localisation string tables, spec-localization-format.md).
// Not fuzzed anywhere before this task - fuzz/fuzz_single.cpp's existing
// Clang-only target list has no `le_strings` branch. The reader's own
// header (include/sr3locstr/le_strings.h) documents that it bounds-checks
// every array/record read deliberately, unlike the real loader which "does
// no bounds check on any count or offset" - exactly the kind of claim worth
// fuzzing to confirm. See fuzz/msvc/README.md.
#include "../fuzz_common.h"
#include "sr3locstr/le_strings.h"

#include <vector>

using fuzzcommon::guarded;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    guarded([&] {
        std::vector<uint8_t> bytes(data, data + size);
        sr3locstr::LeStringsTable t = sr3locstr::parseLeStrings(bytes);
        (void)t.byHash.size();
    });
    return 0;
}
