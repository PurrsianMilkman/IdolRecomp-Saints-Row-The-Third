// sr3player/ground_follow.h - a CHOSEN, deliberately simplified vertical
// ground-follow helper, written for the live-playtest "ground missing" fix
// (2026-10-08): Brad and the parked car were sitting on a flat Y=0 plane
// with a hole showing sky-blue through the ground at their feet, because
// nothing in this project queried any real elevation data. This file is
// the small, generic piece that turns a freshly-sampled real-terrain
// height (sr3zone::SampleHeightmapHeight(), sr3zone/zone_heightmap.h) into
// a per-frame Y value that moves smoothly, never with a hard instant
// teleport - see GroundFollower's own doc comment below for exactly what
// "smoothly" means here and why.
//
// SOURCE OF THE CONTRACT SHAPE (spec-player-runtime.md Sec5.6.4-Sec5.6.6 and
// Sec5.9, read in full for this task - this task's own brief cites
// "Sec5.6.7", but the real file has no such heading; the "Port consequence"
// text it describes is Sec5.9 item 5, confirmed by direct grep against the
// spec file rather than assumed, and quoted from there below). Sec5.9 item
// 5's own "Port consequence" paragraph gives the real, implementable
// per-frame contract a port needs to honour
// without porting Havok's own character-controller internals wholesale:
// compose a per-frame input velocity once; set the capsule's velocity under
// the real gravity rules once per frame (a quadratic terminal-velocity
// falloff against a speed cap when falling, NOT a flat g*dt); integrate
// over the frame's own delta-time; publish velocity/speed/"supported"
// back; update position accordingly. This project has no Havok and is not
// attempting to port the real capsule-cast/contact-response machinery
// (spec-player-runtime.md Sec5.6.5's own support query, step-up/step-down
// raycasts, etc.) - this header's own GroundFollower is this task's
// EXPLICIT, deliberately smaller scope reduction: it honours the
// CONTRACT'S SHAPE (gravity-like behaviour while clearly above the ground,
// snap/ease smoothly once close) using the real sampled heightmap
// elevation as the ground height, not a literal Havok port. Stated
// explicitly, not left implicit: this is a SCOPE REDUCTION, the same one
// this task's own brief names outright - a full capsule-cast/contact-
// response port would be a separate, much larger undertaking.
//
// TWO DIFFERENT CONSUMERS, ONE SHARED PRIMITIVE - read before assuming
// GroundFollower alone is "the physics":
//
//   1. THE PLAYER (tools/sr3_viewer.cpp's runPlaytest(), via
//      sr3player::CharacterProxyDriver, character_collision.h): the
//      player's own vertical motion is ALREADY driven by
//      CharacterProxyDriver::step()'s existing, spec-CONFIRMED gravity
//      formula (spec-player-runtime.md Sec5.6.5 - the quadratic terminal-
//      velocity falloff against controller+0x170's 44.704 cap, "gravity
//      when unsupported, snap to the ground plane when crossing it") -
//      that formula is NOT duplicated or replaced here. What CharacterProxy-
//      Driver lacked before this task is a per-frame-VARYING ground target:
//      its own `groundY_` was a single flat constant for the whole run. A
//      heightmap sample can legitimately differ between two adjacent
//      frames as the player crosses a grid-cell boundary (a real street
//      has gentle slopes 10m apart, but nothing in this project's own
//      reader guarantees a smooth surface - see zone_heightmap.h's own
//      "HIGH CONFIDENCE, not independently confirmed" caveat on the raw
//      value's own meaning). Feeding a newly-sampled raw value STRAIGHT
//      into CharacterProxyDriver::setGroundY() every frame could make the
//      FLOOR ITSELF jump underneath an already-"supported" player - that
//      is exactly the "hard instant teleport" this task's own brief warns
//      against, just one level removed (the target teleports, so the
//      player teleports with it on the very next grounded frame). Here,
//      GroundFollower smooths THAT target before it reaches
//      setGroundY() - the player's own fall/snap behaviour downstream is
//      still the real, CONFIRMED Sec5.6.5 formula, now aimed at a
//      ground height that itself never jumps.
//   2. THE VEHICLE (the same runPlaytest(), PlaytestVehicleRig): has NO
//      vertical physics model of any kind in this project (PlaytestVehicleRig
//      is a planar substep simulator, posX/posZ/heading only - see
//      PlaytestVehicleRig's own doc comment in tools/sr3_viewer.cpp). For
//      the parked car, GroundFollower IS the entire vertical model: its own
//      `easedGroundY()` is read directly as the car's render Y every frame,
//      with THIS file's own simplified gravity-like/snap logic (described
//      below) standing in for the player-side CharacterProxyDriver
//      altogether, since nothing else exists for the vehicle to reuse.
//
// WHAT GroundFollower ACTUALLY DOES (CHOSEN, Sec5.9-item-5-shaped, not a literal
// port of Sec5.6.5's own exact formula - that formula stays inside
// CharacterProxyDriver for the one consumer confirmed to need it exactly):
// each update() call compares the freshly-sampled target against the
// follower's OWN current eased value (not the caller's separate physical
// position - see the two consumers above for why that is the right
// quantity for both). When the gap is within `supportedThreshold`
// (CHOSEN, default 0.1m - small enough to read as "already on the
// ground", large enough not to chatter on floating-point noise), the
// follower reports `supported=true` and closes the remaining gap directly
// (a sub-threshold correction has no visible "teleport" to avoid). When
// the gap exceeds that threshold, the follower reports `supported=false`
// ("falling", in this contract's own vocabulary) and moves toward the
// target by at most `maxStepPerSecond*dt` this frame - a capped per-frame
// vertical step (the task's own suggested CHOSEN mechanism, picked over an
// exponential decay for the same reason SweepCircleVsAabbXZ's own skin-
// margin comment elsewhere in this project prefers a bounded, easy-to-
// reason-about worst case: the largest possible single-frame jump is
// `maxStepPerSecond*dt`, independent of how large the underlying gap is,
// so a badly-anomalous sampled value - e.g. a tile whose Heightmap record
// turns out to share the SEPARATE, independently-flagged ground-mesh-
// origin anomaly some tiles carry, spec-terrain-format.md Sec5 item 2 -
// cannot itself cause an unbounded single-frame visual snap, only a
// bounded-rate drift toward whatever was sampled).

#pragma once

#include <algorithm>
#include <cmath>

namespace sr3player {

// CHOSEN defaults (this task, 2026-10-08) - not spec-derived (no spec
// section states a "how fast should a ported ground-follower ease" rate;
// Sec5.6.5's own real gravity formula, which IS spec-derived, stays inside
// CharacterProxyDriver for its one confirmed consumer - see this file's own
// top comment). 8 m/s is a brisk human jogging-grade vertical rate - fast
// enough that a real sidewalk curb or gentle street slope (this project's
// own 10m heightmap grid spacing, Sec13.5) is crossed in well under a
// second, slow enough that an anomalous outlier sample never produces a
// one-frame jump larger than `8 * dt` metres. 0.1m is a "close enough to
// already be on the ground" tolerance - inside ordinary floating-point/
// grid-resolution noise, far below human-scale step height.
constexpr double kDefaultGroundFollowMaxStepPerSecond = 8.0;
constexpr double kDefaultGroundFollowSupportedThreshold = 0.1;

// One update() call's result.
struct GroundFollowResult {
    // The follower's own current eased height - what a caller feeds onward
    // (CharacterProxyDriver::setGroundY() for the player; the vehicle's own
    // render Y directly - see this file's top comment).
    double groundY = 0.0;
    // true when `groundY` is already within `supportedThreshold` of the
    // most recently sampled target (this update() call's own input) -
    // this CONTRACT's own "supported" bit (Sec5.9 item 5's own "publish...
    // support back"), NOT the same bit CharacterProxyDriver::step()
    // separately computes and publishes for the player's own physical
    // position (that one remains the authority for the player; see this
    // file's own top comment, consumer 1).
    bool supported = true;
};

// Eases a followed quantity's own height toward a freshly-sampled target
// each frame, by at most a capped per-frame step - never a hard, same-frame
// teleport to the new target (see this file's own top comment for exactly
// why that matters for both of this project's two consumers). Stateful by
// design: one instance per followed entity (one for the player's own
// ground-target smoothing, one for the parked vehicle's own render Y -
// exactly mirroring how CharacterProxyDriver itself is "one instance per
// character", sr3player/character_collision.h's own doc comment).
class GroundFollower {
public:
    explicit GroundFollower(double initialGroundY,
                             double maxStepPerSecond = kDefaultGroundFollowMaxStepPerSecond,
                             double supportedThreshold = kDefaultGroundFollowSupportedThreshold)
        : easedGroundY_(initialGroundY), maxStepPerSecond_(maxStepPerSecond),
          supportedThreshold_(supportedThreshold) {}

    // One frame: `rawSampledGroundY` is this frame's freshly-sampled real
    // (or fallback, if no real data was available for this position)
    // target height; `dt` is the frame's own delta-time (seconds). Returns
    // the new eased height plus whether it is now within
    // `supportedThreshold` of that target - see GroundFollowResult's own
    // doc comment. `dt <= 0` is treated the same as "already close enough"
    // (closes the gap directly rather than dividing by/multiplying by a
    // non-positive dt) - mirrors CharacterProxyDriver::step()'s own
    // "dt == 0 does nothing new this frame" convention (spec-player-
    // runtime.md Sec5.6.2) in spirit, without claiming that section's own
    // CONFIRMED status for this file's own CHOSEN logic.
    GroundFollowResult update(double rawSampledGroundY, double dt) {
        GroundFollowResult result;
        const double delta = rawSampledGroundY - easedGroundY_;
        const double absDelta = std::fabs(delta);
        if (dt <= 0.0 || absDelta <= supportedThreshold_) {
            // CHOSEN "supported" case (see this file's top comment): close
            // the remaining (by definition small, or dt-degenerate) gap
            // directly - there is no meaningfully visible "teleport" to
            // avoid for a sub-threshold correction.
            easedGroundY_ = rawSampledGroundY;
            result.supported = true;
        } else {
            // CHOSEN "falling" case: a capped per-frame vertical step
            // toward the target, bounding the worst-case single-frame
            // visual jump to `maxStepPerSecond_ * dt` regardless of how
            // large the underlying gap is (see top comment's own
            // "anomalous outlier sample" paragraph for why that bound
            // matters).
            const double maxStep = maxStepPerSecond_ * dt;
            const double step = (std::min)(absDelta, maxStep);
            easedGroundY_ += (delta > 0.0 ? step : -step);
            result.supported = false;
        }
        result.groundY = easedGroundY_;
        return result;
    }

    double easedGroundY() const { return easedGroundY_; }

private:
    double easedGroundY_;
    double maxStepPerSecond_;
    double supportedThreshold_;
};

}  // namespace sr3player
