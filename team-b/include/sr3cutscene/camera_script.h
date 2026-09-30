// Reader for `.csc_pc` cutscene camera scripts
// (spec-cutscene-camera-format.md).
//
// A file is a list of contiguous camera shots. Each shot carries a float
// start and end time plus eight independent animation channels, and each
// channel is a keyframe track: N times (always 4 bytes each) and N values
// (of a per-channel width). The structure is pinned by an exact-size
// identity:
//
//     record_offset + record_count * 0x68 == file size   (96/96 real files)
//
// parse() enforces that, so a successful parse IS that check re-run per
// file - the same "exact-size replay as structural proof" idiom used by
// sr3morph, sr3foliage and sr3conversation.
//
// THE VALUE WIDTHS ARE NOT IN THE FILE. The engine's own parser only
// fixes up pointers; the consumer knows each channel's element width
// implicitly by index. The spec recovered the widths arithmetically
// instead, from the fact that the keyframe arrays are packed back to back
// with no padding - so each array's start is the previous one's end, and
// `width = span / key_count` falls out. That derivation came back
// UNANIMOUS across all 1,405 shipped records (spec Sec5), which is what
// makes it safe to hard-code in kChannelValueWidth below. It is still
// worth knowing that this is a derived table, not a field this reader
// reads: if a future sample ever disagrees, trust the sample.
//
// DECODED VALUES (spec Sec10, added 2026-09-20 - supersedes the older
// Sec5.1 verdict "channel 1 is not a quaternion", which this file used to
// quote). Where the spec confirms a decode, this header surfaces BOTH the
// raw stored value and the decoded one:
//
//  * Channel 1 is four little-endian s16 (x, y, z, w), decoded as
//    raw * (16385 / 2^28) (Sec10.4). decodeQuaternionsRaw() returns the
//    s16 quads untouched; decodeQuaternions() applies the scale. The
//    scale is NOT 1/32767 - that was the old refuted reading, off by a
//    factor of two (its norm is exactly 0.5).
//  * Channel 2 is FOV in RADIANS in the file (Sec10.5). The decoded FOV in
//    degrees (x 57.2957763671875) is what the engine stores; both are
//    exposed by CameraSample.
//  * Channels 3-7 are depth-of-field controls (Sec10.5): channel 3's
//    PRESENCE is the enable flag, channel 3's value a CoC scale, channels
//    4-7 the four components of the DOF pass's `Focal_params`.
//  * sampleScalarTrack / sampleVec3Track / sampleQuaternionTrack /
//    sampleShot / CameraScript::sample implement the sampling rule of
//    Sec10.3 (linear; shortest-arc slerp for channel 1; hold at both
//    ends; keys are ABSOLUTE cutscene time).
//
// WHAT THIS READER STILL DELIBERATELY DOES NOT ASSERT (spec Sec10.9):
//
//  * Which row/column of the orientation matrix is forward/up/right, and
//    the handedness. quaternionToMatrix() implements the Sec10.4 formula
//    exactly and nothing more (no cutscene-origin basis, no third-row
//    normalisation, no manager anchor transform - Sec10.5/10.9 item 3).
//  * Whether FOV is horizontal or vertical.
//  * Which of the four DOF breakpoints is the near-blur edge, near focus,
//    far focus or far-blur edge. Channels 4-7 are therefore named by
//    index (ordered depth breakpoints is HIGH CONFIDENCE only: 99.27% of
//    keys ascend).
//  * Any cut smoothing downstream of the camera output, the next-shot
//    lookahead's consumer, or the XML-side per-shot DOF enable (Sec10.6/9).
//  * The arithmetic width the engine uses (this reader evaluates the
//    sampling rule in double precision; the spec does not say float vs
//    double for the blend, only the scale constant is stated as a double).
//
// The shot list, not the keyframe data, is authoritative for timing: the
// last shot's end time matches the file's latest keyframe in only 39/96
// files. Sec10.6.3 resolves the tail: every channel holds its last key
// until the shot's end time, then the camera hard-cuts to the next shot
// (or, on the last shot, the clock stops).

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "sr3cutscene/errors.h"
#include "vpp/byte_view.h"

namespace sr3cutscene {

using vpp::ByteView;

constexpr uint16_t kRequiredVersion = 4; // CONFIRMED - the parser rejects anything else
constexpr uint32_t kNullOffset = 0xFFFFFFFFu; // -1 means null, the same idiom as every other format here

constexpr size_t kShotRecordSize = 0x68;  // 104 bytes: 2 floats + 8 channels x 12
constexpr size_t kChannelCount = 8;
constexpr size_t kChannelRecordSize = 12; // { u32 key_count, u32 values_offset, u32 times_offset }
constexpr size_t kTimeWidth = 4;          // times are ALWAYS 4-byte floats on every channel

// Per-channel value element widths, derived from the data region's own
// boundaries and unanimous across 1,405 records (spec Sec5) - see the
// file-level comment for why this is a derived table rather than a field.
constexpr size_t kChannelValueWidth[kChannelCount] = {12, 8, 4, 4, 4, 4, 4, 4};

// Channel indices (spec Sec5 table, as upgraded by Sec10.5).
constexpr size_t kChannelPosition = 0;    // vec3; CONFIRMED (Sec10.5) camera position, linear blend
constexpr size_t kChannelOrientation = 1; // 4 x s16 quaternion (x,y,z,w) * 16385/2^28, slerp (Sec10.4)
constexpr size_t kChannel1 = kChannelOrientation; // older name, kept for existing callers
constexpr size_t kChannelFieldOfView = 2; // float RADIANS in the file (Sec10.5); linear blend
constexpr size_t kChannelOptionalGroupFirst = 3; // 3-7 appear and disappear together (Sec5.2): the DOF group
constexpr size_t kChannelOptionalGroupLast = 7;
constexpr size_t kChannelDofCocScale = 3;    // presence of this channel = DOF enable; value = CoC scale / 2-vs-3-layer switch (Sec10.5)
constexpr size_t kChannelDofFocalFirst = 4;  // 4..7 = Focal_params x,y,z,w; roles of the four breakpoints OPEN (Sec10.9 item 2)
constexpr size_t kChannelDofFocalLast = 7;

// Sec10.4: the s16 quaternion components are multiplied by this DOUBLE.
// 16385 / 2^28 = 2^-14 * (1 + 2^-14) ~ 6.1038882e-5 (so 1.0 ~ 16383). It is
// not 1/32767, 1/32768 (norm 0.5) or 2^-14 (median norm 0.99994).
constexpr double kQuaternionScale = 16385.0 / 268435456.0;

// Sec10.5: the engine multiplies the channel-2 radians by this to store degrees.
constexpr double kRadiansToDegrees = 57.2957763671875;

// One keyframe track. `times` and `valueBytes` are decoded at parse time
// and held by value rather than as views into the caller's buffer: these
// files are small (148 ... 45,240 bytes), so the copy is free, and it
// spares callers the non-owning-view lifetime rules sr3geometry needs.
struct Channel {
    size_t index = 0;      // 0-7; determines valueWidth
    size_t valueWidth = 0; // kChannelValueWidth[index]

    uint32_t keyCount = 0;
    uint32_t valuesOffsetRaw = 0; // as stored; kNullOffset means null
    uint32_t timesOffsetRaw = 0;

    // False when the corresponding offset field is kNullOffset. NOTE: no
    // shipped file uses the null case (0 of 22,480 channel pointers) -
    // empty channels instead carry keyCount 0 with both pointers aimed at
    // the end of the data region. Handled anyway because -1 IS the
    // format's documented null idiom (spec Sec3); an unobserved case is
    // not an impossible one.
    bool hasValues = false;
    bool hasTimes = false;

    // keyCount floats, seconds. Keyframes are authored at 30 fps: across
    // 1,999 multi-key tracks, ALL 1,999 strictly ascending, with the
    // dominant inter-key step 0.0333/0.0334 s (spec Sec1).
    std::vector<float> times;

    // keyCount * valueWidth raw bytes, kept undecoded so every channel
    // shares one representation; use decodeVec3 (width 12),
    // decodeQuaternionsRaw/decodeQuaternions (width 8, channel 1) or
    // decodeFloats (width 4) below.
    std::vector<uint8_t> valueBytes;
};

// One camera shot (0x68 bytes). Shots tile the cutscene's timeline
// exactly - record N starts where record N-1 ended in 1,309/1,309
// consecutive pairs, and the first record of every file starts at 0.0
// (96/96). This reader does NOT enforce that tiling: it is a CONFIRMED
// empirical property of shipped data, not a rule the engine's parser
// checks, so violating it makes a file unusual, not malformed. Use
// isContiguousWith() to test it yourself.
struct Shot {
    size_t offset = 0;    // absolute offset of this record within the parsed content
    float startTime = 0.0f;
    float endTime = 0.0f;
    std::array<Channel, kChannelCount> channels{};

    // True if this shot begins EXACTLY where `previous` ended - bitwise
    // float equality, no tolerance.
    //
    // READ THIS BEFORE USING IT FOR TIMELINE LOGIC. Exact equality is
    // stricter than the property the spec actually measured. Across the
    // real population this reader measured 2,612 of 2,618 consecutive
    // pairs exactly equal, against spec Sec4's 1,309/1,309 (the same
    // 2,618 once byte-identical duplicate entries are counted). The six
    // outliers are NOT gaps: the largest residual is 0.000107 s - about
    // 7 float32 ULP at t~206 s - and there are zero residuals above 1 ms,
    // across just 3 distinct files. The shots really are contiguous; the
    // authoring tool simply didn't write bit-identical start/end floats.
    //
    // It is left as exact equality on purpose: choosing a tolerance would
    // be inventing a rule the spec does not state, and this way the method
    // answers a precise question rather than a fuzzy one. But that means
    // ANY CALLER DOING REAL TIMELINE WORK SHOULD COMPARE WITH ITS OWN
    // EPSILON (1 ms is far below the 33 ms authoring frame and comfortably
    // above the observed residuals) rather than relying on this.
    bool isContiguousWith(const Shot& previous) const { return startTime == previous.endTime; }
};

// A quaternion, components in the file's (x, y, z, w) order.
struct Quat {
    double x = 0.0, y = 0.0, z = 0.0, w = 1.0; // default = identity
};

// The camera evaluated at one absolute cutscene time (spec Sec10.3/10.5).
struct CameraSample {
    size_t shotIndex = 0;

    // Channel 0, linear.
    std::array<double, 3> position{};

    // Channel 1, shortest-arc slerp. NOT renormalised (Sec10.3): its norm
    // is 1 to within the stored precision at a key and dips slightly
    // below 1 between keys.
    Quat orientation;

    // Channel 2, linear, in the file's unit (RADIANS). Sec10.5: the engine
    // stores fovRadians * 57.2957763671875 as degrees; fovDegrees is
    // exactly that product. Horizontal-vs-vertical is not known (Sec10.9).
    double fovRadians = 0.0;
    double fovDegrees = 0.0;

    // Sec10.5: DOF is enabled iff channel 3 has keys (dofEnabled). The
    // remaining fields are meaningful only when dofEnabled; they are the
    // linear samples of channels 3..7 (a missing channel among 4-7
    // samples as 0, per Sec10.3 rule 1; the engine assumes they are
    // present). dofFocalParams[i] is channel 4+i, i.e. Focal_params x,y,z,w;
    // which breakpoint each one is remains OPEN (Sec10.9 item 2).
    bool dofEnabled = false;
    double dofCocScale = 0.0;
    std::array<double, 4> dofFocalParams{};
};

class CameraScript {
public:
    // Parses a whole .csc_pc. Throws FormatError if the version is not 4,
    // if the record-array offset is null (-1) - both rules the real parser
    // enforces (spec Sec3) - if the record array does not end exactly at
    // EOF (the 96/96 identity), or if any channel's time or value array
    // would run past the end of the file. That last check is this reader's
    // own bounds discipline rather than a rule read off the engine, but it
    // matches the population: 0 of 22,480 channel pointers were
    // out-of-range (spec Sec1).
    static CameraScript parse(ByteView content);

    uint16_t version() const { return version_; }

    // Absolute offset of the shot-record array (the fixed-up +0x04 field).
    size_t recordArrayOffset() const { return recordArrayOffset_; }

    // The u32 at +0x08. Zero in 96/96 shipped files AND never read by the
    // engine's parser. Spec Sec3 calls it header padding "to align the
    // keyframe data region to 12"; a measurement made while adding the
    // sampler (tools/validation/validate_csc_sampler.cpp) DISAGREES with
    // that explanation: in 96/96 files the lowest-addressed array is the
    // first shot's channel-0 TIMES array and it starts AT +0x08, and the
    // unique keyframe arrays tile [0x08, record_offset) with no gap or
    // overlap in 96/96 (they do not tile from 0x0C in any file). So these
    // four bytes are (on the shipped data) the first time key of that
    // array - 0.0f, i.e. the first shot's start time, which is why they
    // read zero - not padding. Which side is right is not decided here;
    // this accessor just surfaces the raw word and is deliberately NOT
    // validated (requiring zero would invent a rule).
    uint32_t field_0x08_raw() const { return field_0x08_; }

    const std::vector<Shot>& shots() const { return shots_; }

    // Total timeline length: the last shot's end time, which is
    // authoritative for timing (spec Sec4). Returns 0 for an empty script.
    float duration() const { return shots_.empty() ? 0.0f : shots_.back().endTime; }

    // Index of the shot active at ABSOLUTE cutscene time `t`: the first shot
    // whose endTime is > t, or the last shot when t is at/after the end of
    // the timeline (the engine's clock clamps on the last shot, Sec10.6.1)
    // and shot 0 for t before the first end. This is the start/end-pair scan
    // of Sec6 item 5; it is equivalent to the engine's clock (which
    // accumulates shot durations) only BECAUSE shots tile from 0 (Sec4) - on
    // a file that does not tile, the engine's answer is not specified by
    // the spec and neither is this function's. Throws std::out_of_range on an
    // empty script.
    size_t shotIndexAt(double t) const;

    // sampleShot(shotIndexAt(t), t). See CameraSample.
    CameraSample sample(double t) const;

    // Samples shot `shotIndex` at absolute time `t` (Sec10.3). Throws
    // std::out_of_range if shotIndex >= shots().size(). NOTE: the engine
    // falls back to shot 0 for an out-of-range index (Sec10.6.2); a library
    // API that silently substitutes a different shot would hide bugs, so
    // this throws instead.
    CameraSample sampleShot(size_t shotIndex, double t) const;

private:
    uint16_t version_ = 0;
    size_t recordArrayOffset_ = 0;
    uint32_t field_0x08_ = 0;
    std::vector<Shot> shots_;
};

// Decodes a channel's values as vec3s. Valid only for a channel whose
// valueWidth is 12 (channel 0). Throws FormatError otherwise, rather than
// reinterpreting bytes at a width the format does not use there.
std::vector<std::array<float, 3>> decodeVec3(const Channel& channel);

// Decodes a channel's values as plain floats. Valid only for a channel
// whose valueWidth is 4 (channels 2-7). Throws FormatError otherwise -
// which deliberately includes channel 1: its 8-byte elements are s16
// quads, not floats (use decodeQuaternions / decodeQuaternionsRaw).
std::vector<float> decodeFloats(const Channel& channel);

// Channel 1 as stored: each 8-byte element as four little-endian s16
// (a, b, c, d), untouched (no scale, no reordering). Spec Sec10.4 confirms
// they are (x, y, z, w). Valid only for valueWidth 8; throws FormatError
// otherwise. Counts from the bytes held, not keyCount (like decodeVec3).
std::vector<std::array<int16_t, 4>> decodeQuaternionsRaw(const Channel& channel);

// One raw s16 quad to a quaternion: (x, y, z, w) = (a, b, c, d) *
// kQuaternionScale (Sec10.4). In double: the product is exact to the
// stored constant's precision.
Quat quaternionFromRaw(const std::array<int16_t, 4>& raw);

// decodeQuaternionsRaw + quaternionFromRaw. Same width restriction.
std::vector<Quat> decodeQuaternions(const Channel& channel);

// Row-major 3x3 for a quaternion, exactly the formula of Sec10.4:
//   [[1-2(y^2+z^2), 2(xy-wz),     2(xz+wy)    ],
//    [2(xy+wz),     1-2(x^2+z^2), 2(yz-wx)    ],
//    [2(xz-wy),     2(yz+wx),     1-2(x^2+y^2)]]
// The engine additionally multiplies by an identity "cutscene origin" basis
// and normalises the third row; neither is done here, and which row is
// forward/up/right is not asserted (Sec10.9 item 1).
std::array<std::array<double, 3>, 3> quaternionToMatrix(const Quat& q);

// The Sec10.3 sampling rule for one channel at absolute time `t`. Each
// requires the channel's valueWidth to match (4 / 12 / 8) and throws
// FormatError otherwise. The effective key count is min(keyCount, times
// held, values held): a channel whose pointer was null (unobserved in
// shipped data) therefore samples as empty. Empty -> 0 / zero vector /
// identity quaternion (rule 1); one key or t <= T[0] -> V[0]; t >= T[n-1]
// -> V[n-1] (hold, no extrapolation); otherwise the first index i with
// T[i] > t (linear scan), d = T[i]-T[i-1], d <= 0 -> V[i], else
// f = (t-T[i-1])/d and lerp / shortest-arc slerp (unnormalised result).
// The engine's dormant debug freeze global is ignored (Sec10.3).
double sampleScalarTrack(const Channel& channel, double t);
std::array<double, 3> sampleVec3Track(const Channel& channel, double t);
Quat sampleQuaternionTrack(const Channel& channel, double t);

// Shortest-arc slerp exactly as Sec10.3 states it, exposed for testing:
// c = q0.q1; if c < 0 negate q1 and c; if 1-c > 1e-7 use weights
// sin((1-f)*theta)/sin(theta) and sin(f*theta)/sin(theta) with
// theta = acos(c), else weights 1-f and f. Not renormalised.
Quat slerpShortestArc(const Quat& q0, const Quat& q1, double f);

} // namespace sr3cutscene
