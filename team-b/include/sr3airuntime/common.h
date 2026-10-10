#pragma once

// sr3airuntime/common.h - small shared types for the AI runtime unit
// (spec-ai-runtime.md, units A0-A3). Mirrors the house pattern used by
// other units (e.g. sr3player/movement.h's Vec3/EngineTimer): a survey of
// the codebase before this unit was written found no cross-project shared
// handle/vector/timer type, so each unit defines its own (project
// convention, not an omission).

#include <cstdint>
#include <cmath>
#include <limits>

namespace sr3airuntime {

struct Vec3 {
    double x = 0, y = 0, z = 0;
};
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double LengthSq(Vec3 a) { return Dot(a, a); }
inline double Length(Vec3 a) { return std::sqrt(LengthSq(a)); }
inline double DistanceSq(Vec3 a, Vec3 b) { return LengthSq(a - b); }
inline double Distance(Vec3 a, Vec3 b) { return std::sqrt(DistanceSq(a, b)); }

// Sec9.2 step 5: the chooser's local action block initialises its three
// position vectors to the bit pattern 0x7fffffff (a NaN), read as "position
// not set" (HIGH). Modelled with a real NaN so IsUnsetPosition() is exact.
inline Vec3 UnsetPosition() {
    const double n = std::numeric_limits<double>::quiet_NaN();
    return {n, n, n};
}
inline bool IsUnsetPosition(const Vec3& v) { return std::isnan(v.x) || std::isnan(v.y) || std::isnan(v.z); }

// An engine millisecond timer, as read throughout spec-ai-runtime.md:
// "armed and expired" is 0x00d9e1e0's test (Sec9.1, Sec9.4, Sec9.7); "elapsed
// time on timer X" (Sec9.1's +0x438, Sec9.6's cover/frustrated tests,
// Sec11.6's +0x96c) is 0x00d9e250/0x00d9e210. A single struct covers both
// reads: it remembers when it was armed and for how long, so both "has this
// expired" and "how much time has elapsed since arming" are available.
struct EngineTimer {
    bool armed = false;
    int64_t startMs = 0;
    int64_t durationMs = 0;

    void arm(int64_t nowMs, int64_t durationMs_) {
        armed = true;
        startMs = nowMs;
        durationMs = durationMs_;
    }
    // "Restart" with no new duration (Sec4.1 step 2's "restart +0x43c with 0",
    // Sec9.2 step 4's "restart +0x43c with 0"): duration 0 means "already due".
    void restart(int64_t nowMs, int64_t durationMs_ = 0) { arm(nowMs, durationMs_); }
    void disarm() { armed = false; }
    bool isArmed() const { return armed; }
    int64_t elapsedMs(int64_t nowMs) const { return armed ? nowMs - startMs : 0; }
    int64_t remainingMs(int64_t nowMs) const { return armed ? durationMs - (nowMs - startMs) : 0; }
    // 0x00d9e1e0: "armed and passed". An unarmed timer never expires (the
    // spec's recurring "a timer holding a negative value (never armed) blocks
    // its action", Sec9.7).
    bool expired(int64_t nowMs) const { return armed && remainingMs(nowMs) <= 0; }

    // The "time since X was last seen" idiom (Sec9.8's 0x004fc760: "else
    // 0x7fffffff"): a never-armed timer reads as this sentinel, which compares
    // larger than any real seen_in_last window.
    static constexpr int64_t kNeverSeenMs = 0x7fffffff;
    int64_t elapsedOrNeverMs(int64_t nowMs) const { return armed ? elapsedMs(nowMs) : kNeverSeenMs; }
};

// The 0x58-byte action/committed block (Sec9.2 step 5's local block; Sec11.2's
// field table for what the 70 action routines write into it). Shared by A0
// (which only ever copies it whole: the committed/staged block fields of
// ModeState), A1 (the chooser builds and commits it) and A3 (the routines'
// own writes). Field names follow Sec11.2's offset table; fields with no
// routine writer (Sec11.2 notes mode code arms some of them on the
// COMMITTED copy, not here) are kept for completeness and layout fidelity.
struct ActionBlock {
    static constexpr uint8_t kNoAction = 0x47;  // Sec4.1/Sec9.2: "none"

    uint8_t actionId = kNoAction;    // +0x00
    uint64_t objectHandle = 0;       // +0x08/+0x0c: cover object, threat, ally, vehicle, pickup, victim, prop, corpse (Sec11.2)
    int64_t timer10Ms = 0;           // +0x10: armed by cover code on the COMMITTED copy (human +0x460), not by any routine
    int64_t timer14Ms = 0;           // +0x14: action 67 arms this with 0
    Vec3 destination = UnsetPosition();     // +0x18: "the destination"
    Vec3 secondaryPoint = UnsetPosition();  // +0x24: second point (strike point / away-point / entry point / cover point)
    Vec3 tertiaryVector = UnsetPosition();  // +0x30: launch data / T's position / cover geometry
    uint8_t smallIndex = 0;          // +0x3c: seat (37/38), perception list number (41-43), cleared by 39/67
    uint8_t smallIndex2 = 0;         // +0x3d: cleared alongside +0x3c by 67
    uint32_t value40 = 0;            // +0x40: 0x005422a0's value (11) or the sidestep variant 1-3 (26)
    uint64_t secondaryHandle = 0;    // +0x48/+0x4c: 43's cover object to peek from
    uint8_t flags56 = 0;             // +0x56 bitfield

    // +0x56 bits named per Sec9.4 step 3 (chooser-set bits) and Sec11.2/
    // Sec11.3 (routine-set/read bits).
    static constexpr uint8_t kFlag56_PeekSide = 0x02;      // bit 1: 43's peek side
    static constexpr uint8_t kFlag56_VehEvadeSet = 0x20;   // bit 5: 39 sets this
    static constexpr uint8_t kFlag56_Ordered = 0x40;       // bit 6: set by the chooser's ordered re-assert (Sec9.4 step 3.1)
    static constexpr uint8_t kFlag56_ForcedRecheck = 0x80; // bit 7: set by the chooser (Sec9.4 step 3.2); read by 13
};

// 0x004df080(human, from, to, melee, out) (Sec11.6, used by both A1's goal
// tests - `advance`, `cover` - and A3's action routines - 18, 29, 0/1/2/3's
// engagement logic indirectly via PerceptionState::engageable): -1 when
// `to` is inside range a, +1 when beyond range b, 0 inside the band.
inline int WeaponBandTest(double a, double b, double distance) {
    if (distance < a) return -1;
    if (distance > b) return 1;
    return 0;
}

}  // namespace sr3airuntime
