// Synthetic tests for sr3player::GroundFollower (include/sr3player/
// ground_follow.h) - the CHOSEN, deliberately simplified vertical ground-
// follow helper written for the live-playtest ground-missing fix
// (spec-player-runtime.md Sec5.9 item 5's own "Port consequence"
// contract shape - see that header's own file comment for the full
// citation and the two different consumers this single class serves).
#include <cmath>
#include <iostream>

#include "sr3player/ground_follow.h"

using namespace sr3player;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                      \
    } while (0)

// ===========================================================================
// Already-at-target / within-threshold: reports supported, no drift.
// ===========================================================================

void testExactMatchIsSupported() {
    GroundFollower f(10.0);
    GroundFollowResult r = f.update(10.0, 1.0 / 30.0);
    CHECK(r.supported);
    CHECK(r.groundY == 10.0);
    CHECK(f.easedGroundY() == 10.0);
}

void testWithinThresholdIsSupportedAndClosesGapDirectly() {
    GroundFollower f(10.0, kDefaultGroundFollowMaxStepPerSecond, /*supportedThreshold=*/0.1);
    // 0.05m gap, within the 0.1m threshold - closes directly, not partially.
    GroundFollowResult r = f.update(10.05, 1.0 / 30.0);
    CHECK(r.supported);
    CHECK(r.groundY == 10.05);
}

// ===========================================================================
// Beyond threshold: reports NOT supported ("falling", in this contract's own
// vocabulary) and eases rather than snapping.
// ===========================================================================

void testLargeGapIsNotSupportedOnFirstFrame() {
    GroundFollower f(10.0, /*maxStepPerSecond=*/8.0, /*supportedThreshold=*/0.1);
    // 5m gap, far beyond threshold.
    GroundFollowResult r = f.update(15.0, 1.0 / 30.0);
    CHECK(!r.supported);
    // One frame at 8 m/s over 1/30s moves at most 8/30 ~= 0.2667m - nowhere
    // near the full 5m gap. This IS the "no hard teleport" requirement,
    // checked directly: the result must NOT equal the raw target yet.
    CHECK(r.groundY > 10.0);
    CHECK(r.groundY < 11.0);
    CHECK(std::fabs(r.groundY - (10.0 + 8.0 / 30.0)) < 1e-9);
}

void testEasingConvergesOverSeveralFramesWithoutOvershooting() {
    GroundFollower f(0.0, /*maxStepPerSecond=*/8.0, /*supportedThreshold=*/0.1);
    const double target = 2.0;
    const double dt = 1.0 / 30.0;
    bool everSupported = false;
    double prev = 0.0;
    for (int i = 0; i < 60; ++i) {  // 2 real seconds - comfortably enough at 8 m/s to close a 2m gap
        GroundFollowResult r = f.update(target, dt);
        // Monotonic approach - never overshoots past the target.
        CHECK(r.groundY <= target + 1e-9);
        CHECK(r.groundY >= prev - 1e-9);  // never moves backward away from the target either
        prev = r.groundY;
        if (r.supported) everSupported = true;
    }
    CHECK(everSupported);
    CHECK(std::fabs(f.easedGroundY() - target) < 1e-6);
}

void testCappedStepIndependentOfGapSize() {
    // The whole point of a CAPPED step (vs. e.g. a fixed-fraction
    // exponential decay): the single-frame movement is bounded by
    // maxStepPerSecond*dt NO MATTER how large the underlying gap is - a
    // 5m gap and a 500m gap (e.g. an anomalous outlier sample, see this
    // header's own top comment) must move the SAME amount on frame 1.
    GroundFollower fSmallGap(0.0, 8.0, 0.1);
    GroundFollower fHugeGap(0.0, 8.0, 0.1);
    const double dt = 1.0 / 30.0;
    GroundFollowResult rSmall = fSmallGap.update(5.0, dt);
    GroundFollowResult rHuge = fHugeGap.update(500.0, dt);
    CHECK(!rSmall.supported);
    CHECK(!rHuge.supported);
    CHECK(std::fabs(rSmall.groundY - rHuge.groundY) < 1e-9);
    CHECK(std::fabs(rSmall.groundY - (8.0 / 30.0)) < 1e-9);
}

void testEasesDownwardTooNotJustUpward() {
    // The target can be BELOW the current eased value (e.g. the followed
    // entity crosses from a tile edge onto a lower-lying one) - the same
    // capped-step/no-teleport contract applies symmetrically.
    GroundFollower f(10.0, /*maxStepPerSecond=*/8.0, /*supportedThreshold=*/0.1);
    GroundFollowResult r = f.update(0.0, 1.0 / 30.0);
    CHECK(!r.supported);
    CHECK(r.groundY < 10.0);
    CHECK(std::fabs(r.groundY - (10.0 - 8.0 / 30.0)) < 1e-9);
}

// ===========================================================================
// A sudden target jump never produces an instant same-frame teleport, even
// once the follower was already settled/supported.
// ===========================================================================

void testSuddenTargetJumpAfterSettlingDoesNotTeleport() {
    GroundFollower f(0.0, /*maxStepPerSecond=*/8.0, /*supportedThreshold=*/0.1);
    const double dt = 1.0 / 30.0;
    // Settle at 3.0 first.
    for (int i = 0; i < 30; ++i) f.update(3.0, dt);
    CHECK(std::fabs(f.easedGroundY() - 3.0) < 1e-6);

    // Now the sampled target suddenly jumps to 20.0 (a worst-case
    // discontinuity - e.g. crossing a tile boundary into a differently-
    // shaped grid). The VERY NEXT update() call must not land on/near 20.0.
    GroundFollowResult r = f.update(20.0, dt);
    CHECK(!r.supported);
    CHECK(r.groundY < 4.0);  // nowhere close to the new 20.0 target yet
    CHECK(std::fabs(r.groundY - (3.0 + 8.0 / 30.0)) < 1e-9);
}

// ===========================================================================
// dt <= 0 degenerate case: closes directly rather than dividing/stalling.
// ===========================================================================

void testNonPositiveDtClosesGapDirectly() {
    GroundFollower f(0.0);
    GroundFollowResult r = f.update(100.0, 0.0);
    CHECK(r.supported);
    CHECK(r.groundY == 100.0);

    GroundFollower f2(0.0);
    GroundFollowResult r2 = f2.update(100.0, -1.0);
    CHECK(r2.supported);
    CHECK(r2.groundY == 100.0);
}

void run(const char* name, void (*fn)()) {
    try {
        fn();
    } catch (const std::exception& ex) {
        std::cerr << "CHECK FAILED: " << name << " threw: " << ex.what() << "\n";
        ++g_failures;
    }
}

}  // namespace

int main() {
    run("testExactMatchIsSupported", testExactMatchIsSupported);
    run("testWithinThresholdIsSupportedAndClosesGapDirectly", testWithinThresholdIsSupportedAndClosesGapDirectly);
    run("testLargeGapIsNotSupportedOnFirstFrame", testLargeGapIsNotSupportedOnFirstFrame);
    run("testEasingConvergesOverSeveralFramesWithoutOvershooting",
        testEasingConvergesOverSeveralFramesWithoutOvershooting);
    run("testCappedStepIndependentOfGapSize", testCappedStepIndependentOfGapSize);
    run("testEasesDownwardTooNotJustUpward", testEasesDownwardTooNotJustUpward);
    run("testSuddenTargetJumpAfterSettlingDoesNotTeleport", testSuddenTargetJumpAfterSettlingDoesNotTeleport);
    run("testNonPositiveDtClosesGapDirectly", testNonPositiveDtClosesGapDirectly);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic ground-follow tests passed.\n";
    return 0;
}
