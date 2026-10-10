#pragma once

// sr3binkcodec - planar YUV 4:2:0 output buffer, matching the CONFIRMED
// per-slot plane-pointer record both specs independently document:
//   - spec-bink-boundary.md Sec5.2: the game's own 0x30-byte-stride array of
//     plane-pointer records it builds and hands to BinkRegisterFrameBuffers.
//   - spec-bink-codec.md Sec5.6: the DLL's own decode core resolves its
//     literal pixel-store destination back to this SAME record array -
//     "*(handle+0xd0) + target_slot*0x30 + <field>" - with the full field
//     map below (CONFIRMED).
//
// This is a planar, NOT packed/interleaved, layout - three independent
// (pointer, pitch) pairs (Y, chroma-A, chroma-B), no fourth pair anywhere,
// and NO alpha plane (spec-bink-codec.md Sec5.6, cross-checked against a
// black-box FFmpeg oracle: pix_fmt=yuv420p, not yuva420p, for this game's
// own shipped logo_thq_and_voli_hd.bik).
//
// Limited-range ("TV range") YUV: black/neutral point is Y=16, U=V=128, NOT
// 0/128 (spec-bink-codec.md Sec5.6, cross-checked against ffprobe's own
// color_range=tv on the same real file). This header only documents the
// field layout; see yuv_to_rgb.h for the viewing-only RGB conversion that
// uses this fact.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "sr3luahost/open_state.h"

namespace sr3binkcodec {

// One (pointer, pitch) plane view - either a destination (current frame)
// or a source (previous frame, for the block-copy case). Pitch is in
// bytes-per-row (may exceed width*1 for alignment padding, same as the
// game's own 16-byte-aligned plane allocation, spec-bink-boundary.md
// Sec5.2 point 2).
struct PlaneView {
    uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
    int pitch = 0;  // bytes per row; 0 means "use width" (tightly packed)

    int effectivePitch() const { return pitch != 0 ? pitch : width; }

    uint8_t& at(int x, int y) { return data[static_cast<size_t>(y) * effectivePitch() + x]; }
    const uint8_t& at(int x, int y) const { return data[static_cast<size_t>(y) * effectivePitch() + x]; }
};

// A single plane's own owned backing storage, tightly packed (pitch ==
// width), plus a PlaneView over it. Initial fill value is an explicit,
// clearly-labelled TEST-HARNESS sentinel, passed to resize() below - never
// a claimed real codec fact about what an undecoded region "really" contains.
class OwnedPlane {
public:
    void resize(int width, int height, uint8_t sentinelValue) {
        width_ = width;
        height_ = height;
        storage_.assign(static_cast<size_t>(width) * static_cast<size_t>(height), sentinelValue);
    }
    PlaneView view() { return PlaneView{storage_.data(), width_, height_, width_}; }
    const uint8_t* data() const { return storage_.data(); }
    size_t size() const { return storage_.size(); }
    int width() const { return width_; }
    int height() const { return height_; }

    // Flat-fills every byte of this plane with `value` - a plain
    // convenience (e.g. forcing an undecoded chroma plane to the neutral
    // 128 value for viewing, rather than the generic test sentinel) with
    // no decode-path meaning of its own.
    void fillFlat(uint8_t value) { std::fill(storage_.begin(), storage_.end(), value); }

private:
    std::vector<uint8_t> storage_;
    int width_ = 0;
    int height_ = 0;
};

// spec-bink-boundary.md Sec5.2 point 2 / spec-bink-codec.md Sec5.6's own
// FFmpeg byte-count cross-check (Y=921600 + U=230400 + V=230400 for a
// 1280x720 frame = W*H + 2*((W/2)*(H/2))): CONFIRMED 4:2:0 half-resolution
// chroma, each dimension independently rounded up (ordinary 4:2:0 ceil-div,
// not itself a new reverse-engineered fact - it is the arithmetic the
// CONFIRMED byte-count cross-check above requires).
struct FrameDimensions {
    int lumaWidth = 0;
    int lumaHeight = 0;

    static FrameDimensions fromRoundedLuma(int roundedLumaWidth, int roundedLumaHeight) {
        FrameDimensions d;
        d.lumaWidth = roundedLumaWidth;
        d.lumaHeight = roundedLumaHeight;
        return d;
    }

    int chromaWidth() const { return (lumaWidth + 1) / 2; }
    int chromaHeight() const { return (lumaHeight + 1) / 2; }
};

// A full frame's planar output, owning all three planes. Sentinel bytes
// (distinct per plane, deliberately NOT 16/128/128 so a viewer never
// mistakes "not yet written by this skeleton" for "confirmed decoded
// limited-range black") fill every plane before decode; the decoder only
// overwrites bytes it actually reconstructs.
class PlanarYuvFrame {
public:
    static constexpr uint8_t kUndecodedSentinel = 0x55;

    void allocate(const FrameDimensions& dims) {
        dims_ = dims;
        y_.resize(dims.lumaWidth, dims.lumaHeight, kUndecodedSentinel);
        chromaA_.resize(dims.chromaWidth(), dims.chromaHeight(), kUndecodedSentinel);
        chromaB_.resize(dims.chromaWidth(), dims.chromaHeight(), kUndecodedSentinel);
    }

    const FrameDimensions& dims() const { return dims_; }
    OwnedPlane& luma() { return y_; }
    OwnedPlane& chromaA() { return chromaA_; }
    OwnedPlane& chromaB() { return chromaB_; }
    const OwnedPlane& luma() const { return y_; }
    const OwnedPlane& chromaA() const { return chromaA_; }
    const OwnedPlane& chromaB() const { return chromaB_; }

private:
    FrameDimensions dims_;
    OwnedPlane y_;
    OwnedPlane chromaA_;
    OwnedPlane chromaB_;
};

// The game-side 0x30-byte-stride buffer record (spec-bink-boundary.md
// Sec5.2, spec-bink-codec.md Sec5.6's full field map). This project's own
// decoder never needs to lay out memory in this exact binary shape (it
// owns its planes directly, OwnedPlane above) - this struct exists purely
// as DOCUMENTATION/citation of the CONFIRMED field offsets a from-scratch
// game-side integration would need to reproduce, with the still-OPEN
// fields modelled via sr3luahost::OpenValue (per that header's own
// doc comment: "mechanism confirmed to exist, exact behaviour not yet" -
// a read before set() throws OpenStateError instead of inventing a value).
struct BufferRecordFieldMap {
    // +0x00-+0x18 (7 dwords): NOT individually resolved (spec-bink-codec.md
    // Sec5.6 / Sec8 item 6b) - "plausibly leading base-pointer/byte-size
    // bookkeeping... not touched by the decode core's hot path." No
    // accessor is provided for this range at all (nothing in the CONFIRMED
    // decode path reads it), which is itself the honest way to represent
    // "not touched", distinct from "touched but value unknown".

    // +0x1c: per-slot ready/dirty flag (double-buffer selection). CONFIRMED
    // (spec-bink-codec.md Sec5.2/Sec5.6) but not part of pixel output -
    // modelled as a plain bool, not an OpenValue, since its role (not just
    // its value) is itself fully known.
    bool readyDirtyFlag = false;

    // +0x20: referenced only by call 2 into the per-row workhorse
    // (0x1001cd1b); spec-bink-codec.md Sec5.6 explicitly: "role not
    // pinned... OPEN - not confirmed to be a plane write at all". Modelled
    // with OpenValue so any code that tries to read it before Team A
    // resolves the role gets OpenStateError naming the exact spec gap,
    // never a guessed number.
    sr3luahost::OpenValue<uint32_t> field0x20{"BufferRecord+0x20", "spec-bink-codec.md Sec5.6 call 2 / Sec8 item 6a"};

    // +0x28/+0x2c, +0x34/+0x38, +0x40/+0x44: CONFIRMED (Sec5.6's own table)
    // chroma-A ptr/pitch, chroma-B ptr/pitch, luma ptr/pitch. This project's
    // own PlanarYuvFrame above is the thing that actually owns these three
    // pairs' backing memory; this struct does not duplicate storage, only
    // documents which offset is which per the confirmed field map.
};

}  // namespace sr3binkcodec
