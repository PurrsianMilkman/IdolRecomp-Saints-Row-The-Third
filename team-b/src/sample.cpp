#include "sr3anim/sample.h"

#include <cmath>

namespace sr3anim {

namespace {

// Robust quaternion slerp (shortest path, nlerp fallback when nearly
// parallel to avoid a near-zero divide). Standard form - see e.g. Shoemake
// 1985; nothing project-specific here, this is generic quaternion math.
Quat slerp(const Quat& a, const Quat& b, float f) {
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    Quat bb = b;
    if (dot < 0.0f) {
        bb = {-b.x, -b.y, -b.z, -b.w};
        dot = -dot;
    }
    Quat r;
    if (dot > 0.9995f) {
        r.x = a.x + f * (bb.x - a.x);
        r.y = a.y + f * (bb.y - a.y);
        r.z = a.z + f * (bb.z - a.z);
        r.w = a.w + f * (bb.w - a.w);
    } else {
        float theta0 = std::acos(dot);
        float theta = theta0 * f;
        float sinTheta0 = std::sin(theta0);
        float s0 = std::cos(theta) - dot * std::sin(theta) / sinTheta0;
        float s1 = std::sin(theta) / sinTheta0;
        r.x = s0 * a.x + s1 * bb.x;
        r.y = s0 * a.y + s1 * bb.y;
        r.z = s0 * a.z + s1 * bb.z;
        r.w = s0 * a.w + s1 * bb.w;
    }
    float n = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
    if (n > 1e-12f) {
        r.x /= n; r.y /= n; r.z /= n; r.w /= n;
    }
    return r;
}

Vec3 lerpVec3(const Vec3& a, const Vec3& b, float f) {
    return {a.x + f * (b.x - a.x), a.y + f * (b.y - a.y), a.z + f * (b.z - a.z)};
}

// Finds bracketing indices (lo, hi) into `times` (size n, non-decreasing)
// for `t`, plus the interpolation fraction. Clamps at both ends. n must be
// >= 1 (caller checks).
void bracket(const std::vector<float>& times, float t, size_t& lo, size_t& hi, float& frac) {
    const size_t n = times.size();
    if (n == 1 || t <= times.front()) {
        lo = hi = 0;
        frac = 0.0f;
        return;
    }
    if (t >= times.back()) {
        lo = hi = n - 1;
        frac = 0.0f;
        return;
    }
    for (size_t k = 0; k + 1 < n; ++k) {
        if (t >= times[k] && t <= times[k + 1]) {
            lo = k;
            hi = k + 1;
            float span = times[hi] - times[lo];
            frac = (span > 1e-9f) ? (t - times[lo]) / span : 0.0f;
            return;
        }
    }
    // Should not be reached given the front/back clamps above.
    lo = hi = n - 1;
    frac = 0.0f;
}

} // namespace

std::vector<BoneSample> sampleClipAtTime(ByteView bytes, const Animation& a, const Payload& payload,
                                          size_t boneCount, float timeUnits) {
    std::vector<BoneSample> out(boneCount);

    const float duration = static_cast<float>(a.durationTotal());
    float t = timeUnits;
    if (duration > 0.0f) {
        if (t < 0.0f) t = 0.0f;
        if (t > duration) t = duration;
    } else {
        t = 0.0f;
    }

    const auto& tracks = payload.tracks();
    const size_t trackCount = tracks.size();

    const bool hasTable = a.hasTrackBoneTable();
    const uint32_t tableOffset = a.trackBoneTableOffset();

    for (size_t trackIdx = 0; trackIdx < trackCount; ++trackIdx) {
        size_t boneIdx = trackIdx;
        if (hasTable) {
            size_t at = static_cast<size_t>(tableOffset) + trackIdx;
            if (at >= bytes.size()) continue; // malformed - defensively skip
            boneIdx = bytes.at(at);
        }
        if (boneIdx >= boneCount) continue; // caller's rig doesn't cover this track

        BoneSample& sample = out[boneIdx];
        const TrackBlock& blk = tracks[trackIdx];

        // --- Rotation: ASSUMED uniform spacing over [0, duration] (see
        // this header's/this file's top-of-header time-model comment).
        std::vector<RotationSample> rots = payload.rotations(bytes, trackIdx);
        if (!rots.empty()) {
            sample.hasRotationTrack = true;
            if (rots.size() == 1 || duration <= 0.0f) {
                sample.rotation = rots.front().value;
            } else {
                std::vector<float> times(rots.size());
                const float step = duration / static_cast<float>(rots.size() - 1);
                for (size_t k = 0; k < rots.size(); ++k) times[k] = step * static_cast<float>(k);
                size_t lo, hi;
                float frac;
                bracket(times, t, lo, hi, frac);
                sample.rotation = (lo == hi) ? rots[lo].value : slerp(rots[lo].value, rots[hi].value, frac);
            }
        }

        // --- Translation: REAL per-key duration bytes (payload.h /
        // HANDOFF Sec9.43 - CONFIRMED as a per-key hold duration; unit
        // unidentified but consistent with header +0x06's unit, which is
        // all that matters for building a within-track cumulative time).
        std::vector<Vec3> trans = payload.translations(bytes, trackIdx);
        if (!trans.empty()) {
            sample.hasTranslationTrack = true;
            if (trans.size() == 1 || duration <= 0.0f) {
                const Vec3& v = trans.front();
                sample.translationDelta = {v.x, v.y, v.z};
            } else {
                const size_t n = trans.size();
                const size_t durOffset = blk.translationSampleOffset + n * 3;
                std::vector<float> times(n);
                float running = 0.0f;
                bool durationBytesInRange = (durOffset + n) <= bytes.size();
                for (size_t k = 0; k < n; ++k) {
                    times[k] = running;
                    if (durationBytesInRange) {
                        running += static_cast<float>(bytes.at(durOffset + k));
                    } else {
                        // Malformed/short read - fall back to uniform
                        // spacing across the clip so this track still
                        // degrades gracefully rather than collapsing to a
                        // single point.
                        running += (duration / static_cast<float>(n));
                    }
                }
                size_t lo, hi;
                float frac;
                bracket(times, t, lo, hi, frac);
                Vec3 v = (lo == hi) ? trans[lo] : lerpVec3(trans[lo], trans[hi], frac);
                sample.translationDelta = {v.x, v.y, v.z};
            }
        }
    }

    return out;
}

} // namespace sr3anim
