#include "sr3anim/payload.h"

#include <cmath>
#include <cstring>
#include <string>

namespace sr3anim {

namespace {

// Sec6c.1 writes these as `align2(p+1)` and `align4(p+3)`. Those are the C
// IDIOMS `(p+1) & ~1` and `(p+3) & ~3` - i.e. align p UP - not "call an
// align function on p+1". Reading them the second way applies the offset
// twice, and that single mistake took the walk from 99.89% to 2.5%: it was
// the whole of the original failure. Measured, not argued - see
// tools/validation/probe_anim_walk.cpp, which scored 128 readings against
// the +0x30 oracle.
size_t alignUp2(size_t p) { return (p + 1) & ~static_cast<size_t>(1); }
size_t alignUp4(size_t p) { return (p + 3) & ~static_cast<size_t>(3); }

int16_t readI16(ByteView b, size_t at) {
    return static_cast<int16_t>(b.readU16LE(at));
}
int32_t readI32(ByteView b, size_t at) {
    return static_cast<int32_t>(b.readU32LE(at));
}

// Sec6c.2: the four permutation rows, read from the binary. Row k says
// "insert the reconstructed component at position k" - the canonical
// smallest-three arrangement.
const uint8_t kPermutations[4][4] = {
    {3, 0, 1, 2},
    {0, 3, 1, 2},
    {0, 1, 3, 2},
    {0, 1, 2, 3},
};

// Sec6c.2: the 64-entry adaptive step table is exactly the regular 4x4x4
// product of {1, 2, 4, 16}, so it is generated rather than transcribed -
// a transcribed table of 64 magic numbers is a copying hazard, and the
// spec states the closed form explicitly.
const float kStepSet[4] = {1.0f, 2.0f, 4.0f, 16.0f};

// Sign-extend a 6-bit field to int: [0,63] -> [-32,31].
int signExtend6(unsigned v) {
    v &= 0x3Fu;
    return static_cast<int>(v) - ((v & 0x20u) ? 64 : 0);
}

} // namespace

Payload Payload::walk(ByteView bytes, const Animation& a) {
    Payload out;

    const uint8_t flags = a.flags();
    const bool wideCounts = (flags & kWideCountsFlag) != 0;
    const bool wideTranslation = (flags & kWideTranslationFlag) != 0;
    out.hasExtra_ = (flags & kExtraPayloadFlag) != 0;

    // The payload starts after the header AND the track->bone table when
    // one is present (Sec6b / HANDOFF 9.31). The table offset doubles as
    // the header size, so it is exactly where the header ends.
    size_t p = a.hasTrackBoneTable() ? a.trackBoneTableOffset() : 0;
    if (p == 0) {
        p = a.hasOptionalSection() ? 0x48 : 0x38;
    } else {
        // Table present: it holds one byte per track, immediately after
        // the header, and the payload follows it.
        p += a.field_0x0A_rawCount();  // one byte per track (Sec6b)
    }

    const size_t trackCount = a.field_0x0A_rawCount();  // Sec2 +0x0A: animated track count
    out.tracks_.reserve(trackCount);

    // Every read below is bounds-checked before it happens. A clip that
    // cannot complete the walk is a KNOWN population (Sec6c.4 - the 123
    // that overrun are almost all flags-0x40 clips), so it returns an
    // incomplete Payload instead of throwing: refusing to distinguish
    // "this file is broken" from "this reader does not yet cover this
    // shape" would hide the very thing that needs reporting.
    const size_t size = bytes.size();
    auto need = [&](size_t at, size_t n) { return at + n <= size; };

    for (size_t t = 0; t < trackCount; ++t) {
        TrackBlock blk;
        blk.blockStart = p;

        uint32_t rotKeys = 0, transKeys = 0;
        if (!wideCounts) {
            if (!need(p, 2)) return out;
            rotKeys = bytes.at(p);
            transKeys = bytes.at(p + 1);
            p += 2;
        } else {
            p = alignUp2(p);
            if (!need(p, 4)) return out;
            rotKeys = bytes.readU16LE(p);
            transKeys = bytes.readU16LE(p + 2);
            p += 4;
        }
        blk.rotationKeys = rotKeys;
        blk.translationKeys = transKeys;

        // Rotation control records: 4 bytes each, low 6 bits of byte 3 is
        // a run span. Walk until the spans cover every key.
        blk.rotationControlOffset = p;
        size_t n = 0;
        uint64_t acc = 0;
        while (acc < rotKeys) {
            if (!need(p + n * 4, 4)) return out;
            acc += 1u + (bytes.at(p + n * 4 + 3) & 0x3Fu);
            ++n;
        }
        blk.rotationControlCount = n;
        p += n * 4;

        blk.rotationSampleOffset = p;
        if (!need(p, static_cast<size_t>(rotKeys) * 3)) return out;
        p += static_cast<size_t>(rotKeys) * 3;

        // Translation control records.
        size_t m = 0;
        acc = 0;
        if (!wideTranslation) {
            p = alignUp2(p);
            blk.translationControlStride = 8;
            blk.translationControlOffset = p;
            while (acc < transKeys) {
                if (!need(p + m * 8, 8)) return out;
                const uint32_t span = bytes.readU16LE(p + m * 8);
                // A span of 0 is a MISPARSE SIGNATURE, not padding. Added
                // 2026-09-12 (HANDOFF Sec9.48) after this reader silently
                // skipped such records and reported walkComplete() on walks
                // that had demonstrably left the record stream.
                //
                // Evidence: span==0 occurs in 25 records across 14 clips and
                // in ZERO of the 3,687 clips that land on their declared
                // endpoint. Of those 14, eleven have a successor record whose
                // span reads 12,682-65,099 - indistinguishable from reading
                // misaligned bytes, against a validated plausibility profile
                // that accepts real records 94.39% and misaligned ones 0.00%
                // (+1/+3/+5). The remaining three have no second translation
                // track, so they never get the chance to drift; their sibling
                // clips do drift the moment one exists.
                //
                // Refusing costs nothing on the working population - no clip
                // that currently lands contains one - and stops this reader
                // claiming success for a cursor that is no longer on records.
                if (span == 0) return out;
                acc += span;
                ++m;
            }
            p += m * 8;
        } else {
            p = alignUp4(p);
            blk.translationControlStride = 16;
            blk.translationControlOffset = p;
            while (acc < transKeys) {
                if (!need(p + m * 16, 16)) return out;
                const int32_t span = readI32(bytes, p + m * 16);
                if (span <= 0) return out; // a non-advancing span would spin
                acc += static_cast<uint64_t>(span);
                ++m;
            }
            p += m * 16;
        }
        blk.translationControlCount = m;

        blk.translationSampleOffset = p;
        // 3 bytes per key, then one more byte per key (Sec6c.1).
        if (!need(p, static_cast<size_t>(transKeys) * 4)) return out;
        p += static_cast<size_t>(transKeys) * 3;
        p += static_cast<size_t>(transKeys);

        blk.blockEnd = p;
        out.tracks_.push_back(blk);
    }

    out.walkComplete_ = true;
    out.endCursor_ = p;
    if (a.hasTrailingOffset()) {
        out.landedOnEnd_ = (p == a.trailingOffset());
    }
    return out;
}

std::vector<Vec3> Payload::translations(ByteView bytes, size_t trackIndex) const {
    std::vector<Vec3> out;
    if (trackIndex >= tracks_.size()) return out;
    const TrackBlock& blk = tracks_[trackIndex];
    out.reserve(blk.translationKeys);

    // Each control record carries a span and a base position; the sample
    // stream contributes a signed byte delta per axis per key.
    // Sec6c.5: axis = base/64 + delta/4000.
    size_t key = 0;
    for (size_t r = 0; r < blk.translationControlCount && key < blk.translationKeys; ++r) {
        const size_t at = blk.translationControlOffset + r * blk.translationControlStride;
        uint64_t span = 0;
        float bx = 0.0f, by = 0.0f, bz = 0.0f;
        if (blk.translationControlStride == 8) {
            span = bytes.readU16LE(at);
            bx = static_cast<float>(readI16(bytes, at + 2));
            by = static_cast<float>(readI16(bytes, at + 4));
            bz = static_cast<float>(readI16(bytes, at + 6));
        } else {
            span = static_cast<uint64_t>(readI32(bytes, at));
            bx = static_cast<float>(readI32(bytes, at + 4));
            by = static_cast<float>(readI32(bytes, at + 8));
            bz = static_cast<float>(readI32(bytes, at + 12));
        }
        for (uint64_t s = 0; s < span && key < blk.translationKeys; ++s, ++key) {
            const size_t sa = blk.translationSampleOffset + key * 3;
            if (sa + 3 > bytes.size()) return out;
            const int8_t dx = static_cast<int8_t>(bytes.at(sa + 0));
            const int8_t dy = static_cast<int8_t>(bytes.at(sa + 1));
            const int8_t dz = static_cast<int8_t>(bytes.at(sa + 2));
            Vec3 v;
            v.x = bx * kTranslationBaseScale + static_cast<float>(dx) * kTranslationDeltaScale;
            v.y = by * kTranslationBaseScale + static_cast<float>(dy) * kTranslationDeltaScale;
            v.z = bz * kTranslationBaseScale + static_cast<float>(dz) * kTranslationDeltaScale;
            out.push_back(v);
        }
    }
    return out;
}

std::vector<RotationSample> Payload::rotations(ByteView bytes, size_t trackIndex) const {
    std::vector<RotationSample> out;
    if (trackIndex >= tracks_.size()) return out;
    const TrackBlock& blk = tracks_[trackIndex];
    out.reserve(blk.rotationKeys);

    size_t key = 0;
    for (size_t r = 0; r < blk.rotationControlCount && key < blk.rotationKeys; ++r) {
        const size_t at = blk.rotationControlOffset + r * 4;
        if (at + 4 > bytes.size()) return out;

        const uint8_t b0 = bytes.at(at + 0);
        const uint8_t b1 = bytes.at(at + 1);
        const uint8_t b2 = bytes.at(at + 2);
        const uint8_t b3 = bytes.at(at + 3);

        const uint32_t span = 1u + (b3 & 0x3Fu);
        const uint8_t permIndex = static_cast<uint8_t>(b3 >> 6);

        // Sec6c.2 / Sec6c.3 item 4: the top 2 bits of bytes 0-2 form the
        // per-axis step index; the remaining 6 bits are the sample bases.
        // This half is [HIGH CONFIDENCE - inferred], not confirmed - it is
        // the one piece never replayed against file bytes, and it is why
        // rotations from this reader carry a lower tier than translations.
        // CORRECTED 2026-09-11 against the spec's new exhaustive bit table.
        // Two things this reader had wrong, both from prose that has since
        // been fixed at the source:
        //   * the base is a SIGNED 6-bit field, [-32, 31] - not unsigned
        //     0..63 as the first implementation here assumed;
        //   * the step index comes from the CONTROL RECORD bytes. The old
        //     prose said "sample bytes", which is not merely mislabelled
        //     but unimplementable, since the delta packing consumes those
        //     bits. This reader happened to take it from the control bytes
        //     already, so that half was right by luck, not by reading.
        const float stepX = kStepSet[(b0 >> 6) & 0x3u];
        const float stepY = kStepSet[(b1 >> 6) & 0x3u];
        const float stepZ = kStepSet[(b2 >> 6) & 0x3u];
        const int baseX = signExtend6(b0 & 0x3Fu);
        const int baseY = signExtend6(b1 & 0x3Fu);
        const int baseZ = signExtend6(b2 & 0x3Fu);

        for (uint32_t s = 0; s < span && key < blk.rotationKeys; ++s, ++key) {
            const size_t sa = blk.rotationSampleOffset + key * 3;
            if (sa + 3 > bytes.size()) return out;
            // The three sample bytes are 24 bits consumed ENTIRELY by the
            // delta fields: three 6-bit signed deltas (18 bits) plus 6
            // unused. The first implementation here read them as three
            // independent int8s, which both over-reads each delta and
            // mis-splits them.
            const uint8_t s0 = bytes.at(sa + 0);
            const uint8_t s1 = bytes.at(sa + 1);
            const uint8_t s2 = bytes.at(sa + 2);
            const int dx = signExtend6(s0 >> 2);
            const int dy = signExtend6(((s0 & 0x03u) << 4) | (s1 >> 4));
            const int dz = signExtend6(((s1 & 0x0Fu) << 2) | (s2 >> 6));

            // THE ASSEMBLY. Recovered from a worked example rather than
            // from the spec text, which states only the achievable extreme
            // `(16*32 + 32*64) * 4 * SCALE = 0.884` and never writes the
            // expression itself. Solving that against three worked
            // components supplied by Team A gives, exactly on all three
            // axes:
            //
            //     component = (base * 64 + mult * delta) * 4 * SCALE
            //
            // This reader previously had `(base + mult * delta) * SCALE` -
            // missing the *64 on the base and the *4 overall. That, and
            // nothing else, is what capped it at 0.181 per component and
            // produced the bogus ~36-degree ceiling. Base width was never
            // the problem: this reader read the full 6 bits throughout.
            const double cx = (static_cast<double>(baseX) * 64.0 +
                               stepX * static_cast<double>(dx)) * 4.0 * kRotationScale;
            const double cy = (static_cast<double>(baseY) * 64.0 +
                               stepY * static_cast<double>(dy)) * 4.0 * kRotationScale;
            const double cz = (static_cast<double>(baseZ) * 64.0 +
                               stepZ * static_cast<double>(dz)) * 4.0 * kRotationScale;

            RotationSample rs;
            const double sumSq = cx * cx + cy * cy + cz * cz;
            double arg = kRotationK - sumSq;
            if (arg < 0.0) {
                arg = 0.0;
                rs.clamped = true;
            }
            const double recon = std::sqrt(arg);

            // Insert the reconstructed component at the permutation's
            // position; the three stored components fill the rest in order.
            // Direction matters and is easy to invert. The rows are
            // `q[i] = lanes[perm[i]]`, NOT `q[perm[i]] = lanes[i]`: with
            // lanes = {cx, cy, cz, recon}, row 0 = [3,0,1,2] then puts
            // `recon` at output position 0, row 1 at position 1, and so on
            // - which is exactly what Sec6c.2's "insert the reconstructed
            // component at position k" says. Read the other way round the
            // rows would place it at 2,1,2,3 and silently mis-orient every
            // bone, so this is asserted rather than assumed: see the
            // smallest-three oracle in validate_anim_payload.
            const uint8_t* perm = kPermutations[permIndex];
            const double lanes[4] = {cx, cy, cz, recon};
            double q[4] = {0.0, 0.0, 0.0, 0.0};
            for (int i = 0; i < 4; ++i) q[i] = lanes[perm[i]];
            rs.reconstructedLane = permIndex;

            rs.value.x = static_cast<float>(q[0]);
            rs.value.y = static_cast<float>(q[1]);
            rs.value.z = static_cast<float>(q[2]);
            rs.value.w = static_cast<float>(q[3]);
            out.push_back(rs);
        }
    }
    return out;
}

} // namespace sr3anim
