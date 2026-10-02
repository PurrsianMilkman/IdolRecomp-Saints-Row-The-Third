// UI resolution state behind vint_is_std_res / vint_get_safe_frame
// (spec-lua-api-behaviour.md Sec26.26, 2026-10-01 text through job nnlt).
// Declarations: engine_state.h ("UI resolution queries").
//
// REAL (CONFIRMED by Sec26.26's review status): the per-thread record's two
// integers are copies of the global width 0x02a5a180 / height 0x02a5a184;
// their writers are the UI subsystem init 0x00e23910 and 0x00e23a20(width,
// height); both recompute the mode through 0x00e23000 and the C-side
// standard-resolution test 0x00e2ad30; 0x00e23a20's change-time Lua calls;
// the mode ladder; the two safe-frame constants.
// HIGH CONFIDENCE in the spec, so refused rather than used: the four double
// thresholds of the ladder (only reached for a ratio above 2.48).
#include "sr3luahost/engine_state.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace sr3luahost {

namespace {

const char* const kSpec = "spec-lua-api-behaviour.md Sec26.26";

} // namespace

double EngineState::doubleFromBits(uint64_t bits) {
    double d;
    static_assert(sizeof(d) == sizeof(bits), "double must be 64-bit");
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

int32_t EngineState::vintDisplayModeLadder(int32_t a, int32_t b) {
    // a / b in single precision (REAL). Division by zero spelled out (IEEE:
    // +-inf, NaN for 0/0) so no sanitizer sees it.
    const float fa = static_cast<float>(a);
    const float fb = static_cast<float>(b);
    float r;
    if (fb != 0.0f) {
        r = fa / fb;
    } else {
        r = fa == 0.0f ? std::numeric_limits<float>::quiet_NaN()
                       : (fa > 0.0f ? std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::infinity());
    }
    // Mode -1, then strict tests. The first, > 2.48, uses the single at
    // 0x01256908 (REAL); a NaN fails it like every ordered compare.
    if (!(r > 2.48f)) return -1;
    // Above 2.48 the doubles at 0x01256900 / 0x012568f8 / 0x012568f0 /
    // 0x012568e8 (3.18 / 3.72 / 4.77 / 5.58, "HIGH CONFIDENCE as rendered")
    // decide between modes 0..4: not cleared for implementation.
    throw OpenStateError("display mode 0x0132bd80 for a width/height ratio above 2.48 (the ladder's double "
                         "thresholds 3.18/3.72/4.77/5.58 are HIGH CONFIDENCE only)",
                         kSpec);
}

int32_t EngineState::vintLayoutIndexFor(int32_t width, int32_t height, int32_t mode) {
    // 0x00e2ad30: "the same rule as this function: width / height < 1.5 or
    // mode 2" (REAL); 1 = standard, 0 = wide (labels HIGH CONFIDENCE).
    const double w = static_cast<double>(width);
    const double h = static_cast<double>(height);
    double q;
    if (h != 0.0) {
        q = w / h;
    } else {
        q = w == 0.0 ? std::numeric_limits<double>::quiet_NaN()
                     : (w > 0.0 ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity());
    }
    return (q < 1.5 || mode == 2) ? 1 : 0;
}

void EngineState::vintUiSubsystemInit(int32_t width, int32_t height) {
    // 0x00e23910: the pair from its second argument into the globals and the
    // calling thread's record (0x00e237f0), then the mode (0x00e23000) and
    // the layout index (0x00e2ad30).
    vintGlobalWidth_.set(width);
    vintGlobalHeight_.set(height);
    vintRecordFirst_.set(width);
    vintRecordSecond_.set(height);
    try {
        const int32_t mode = vintDisplayModeLadder(width, height);
        vintDisplayMode_.set(mode);
        vintLayoutIndex_.set(vintLayoutIndexFor(width, height, mode));
    } catch (const OpenStateError&) {
        // The mode is OPEN for this ratio, and so is the layout index that
        // depends on it - unless the aspect test alone decides it (< 1.5),
        // which cannot happen above 2.48.
        vintDisplayMode_.forget();
        vintLayoutIndex_.forget();
    }
}

bool EngineState::vintResolutionChange(int32_t width, int32_t height) {
    // 0x00e23a20(width, height). Every read happens before any write.
    const int32_t oldWidth = vintGlobalWidth_.get();
    const int32_t oldHeight = vintGlobalHeight_.get();
    if (oldWidth == width && oldHeight == height) return false; // unchanged: nothing
    const int32_t oldLayout = vintLayoutIndex_.get();
    const int32_t mode = vintDisplayModeLadder(width, height);
    const int32_t layout = vintLayoutIndexFor(width, height, mode);
    vintGlobalWidth_.set(width);
    vintGlobalHeight_.set(height);
    vintRecordFirst_.set(width);
    vintRecordSecond_.set(height);
    vintDisplayMode_.set(mode);
    vintLayoutIndex_.set(layout);
    // The UI-state global vint_lib_init_constants() if it is a function.
    bool errored = false;
    if (screenFadeCallUi("vint_lib_init_constants", 0, 0, 0, 0, &errored)) ++vintLibInitConstantsCalls_;
    // Standard/wide class flipped: <document name>_reset() for every loaded
    // UI document that defines it (0x00e21670). No UI document list here.
    if (layout != oldLayout) ++vintDocumentResetsNotModelled_;
    return true;
}

double EngineState::vintSafeFrameRound(double product) {
    // Round to nearest (Sec26.26 host note). Ties cannot occur for the
    // CONFIRMED constants and any |dimension| < 2^23; for an exact tie the
    // rounding mode is not stated, and an int32 overflow has no stated result.
    if (!(std::fabs(product) < 2147483647.5)) {
        throw OpenStateError("safe-frame product outside int32 (result not stated)", kSpec);
    }
    const double fl = std::floor(product);
    const double frac = product - fl;
    if (frac == 0.5) throw OpenStateError("safe-frame product at an exact .5 tie (rounding mode not stated)", kSpec);
    return frac < 0.5 ? fl : fl + 1.0;
}

} // namespace sr3luahost
