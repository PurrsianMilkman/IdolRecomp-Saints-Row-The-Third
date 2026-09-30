#include "sr3cutscene/camera_script.h"

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace sr3cutscene {

namespace {

// Little-endian s16 (the reader, like its float reads, assumes a
// little-endian host: memcpy of the stored bytes).
int16_t readS16(const uint8_t* p) {
    int16_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

float readF32LE(ByteView bytes, size_t offset) {
    uint32_t raw = bytes.readU32LE(offset);
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(raw), "f32 must be 4 bytes");
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

// Reads one channel's keyframe arrays out of the file. `shotIndex` and
// `channelIndex` are carried purely so a bounds failure can say WHICH
// channel of WHICH shot was bad - a residual that localises the problem,
// the same way sr3foliage reports its chain-end residual.
Channel readChannel(ByteView content, size_t at, size_t channelIndex, size_t shotIndex) {
    Channel channel;
    channel.index = channelIndex;
    channel.valueWidth = kChannelValueWidth[channelIndex];
    channel.keyCount = content.readU32LE(at + 0x00);
    channel.valuesOffsetRaw = content.readU32LE(at + 0x04);
    channel.timesOffsetRaw = content.readU32LE(at + 0x08);
    channel.hasValues = channel.valuesOffsetRaw != kNullOffset;
    channel.hasTimes = channel.timesOffsetRaw != kNullOffset;

    const std::string where = "shot " + std::to_string(shotIndex) + " channel " +
                              std::to_string(channelIndex);

    if (channel.keyCount != 0 && channel.hasTimes) {
        size_t bytes = static_cast<size_t>(channel.keyCount) * kTimeWidth;
        if (channel.timesOffsetRaw > content.size() ||
            bytes > content.size() - channel.timesOffsetRaw) {
            throw FormatError(where + "'s time array (" + std::to_string(channel.keyCount) +
                              " keys x 4 bytes at offset " +
                              std::to_string(channel.timesOffsetRaw) +
                              ") runs past the end of the file");
        }
        channel.times.reserve(channel.keyCount);
        for (uint32_t k = 0; k < channel.keyCount; ++k) {
            channel.times.push_back(
                readF32LE(content, channel.timesOffsetRaw + static_cast<size_t>(k) * kTimeWidth));
        }
    }

    if (channel.keyCount != 0 && channel.hasValues) {
        size_t bytes = static_cast<size_t>(channel.keyCount) * channel.valueWidth;
        if (channel.valuesOffsetRaw > content.size() ||
            bytes > content.size() - channel.valuesOffsetRaw) {
            throw FormatError(where + "'s value array (" + std::to_string(channel.keyCount) +
                              " keys x " + std::to_string(channel.valueWidth) +
                              " bytes at offset " + std::to_string(channel.valuesOffsetRaw) +
                              ") runs past the end of the file");
        }
        ByteView values = content.subview(channel.valuesOffsetRaw, bytes);
        channel.valueBytes.assign(values.data(), values.data() + values.size());
    }

    return channel;
}

} // namespace

CameraScript CameraScript::parse(ByteView content) {
    CameraScript script;

    // Spec Sec3 states a 12-byte header (u16 version, u16 count, u32 record
    // offset, u32 padding) with the keyframe data region starting at +0x0C.
    // Measured on the shipped files, the data region starts at +0x08 (see
    // CameraScript::field_0x08_raw); the 12-byte minimum here follows the
    // spec's text and is harmless on shipped data (smallest file: 148 bytes).
    if (content.size() < 0x0C) {
        throw FormatError("content too small to contain the .csc_pc header (12 bytes)");
    }

    script.version_ = content.readU16LE(0x00);
    if (script.version_ != kRequiredVersion) {
        throw FormatError("unsupported .csc_pc version " + std::to_string(script.version_) +
                          ": the parser requires exactly " + std::to_string(kRequiredVersion) +
                          " (spec Sec3, CONFIRMED - disassembly + 96/96)");
    }

    uint16_t shotCount = content.readU16LE(0x02);
    uint32_t recordOffset = content.readU32LE(0x04);
    script.field_0x08_ = content.readU32LE(0x08);

    // Null record-array offset: the real parser rejects the file outright
    // (spec Sec3), so this is a hard error rather than an empty script.
    if (recordOffset == kNullOffset) {
        throw FormatError("shot-record array offset (+0x04) is null (-1): the parser rejects "
                          "the file (spec Sec3, CONFIRMED)");
    }
    script.recordArrayOffset_ = recordOffset;

    // --- The exact-size identity: the record array ends precisely at EOF
    // in 96/96 shipped files (spec Sec1/Sec3). Checked rather than
    // assumed, and phrased with both sides so a mismatch localises. ---
    size_t arrayBytes = static_cast<size_t>(shotCount) * kShotRecordSize;
    if (recordOffset > content.size() || arrayBytes > content.size() - recordOffset) {
        throw FormatError("shot-record array (" + std::to_string(shotCount) + " x 0x68 at offset " +
                          std::to_string(recordOffset) + ") runs past the end of the file (" +
                          std::to_string(content.size()) + " bytes)");
    }
    if (recordOffset + arrayBytes != content.size()) {
        throw FormatError("size mismatch: " + std::to_string(shotCount) +
                          " shot records at offset " + std::to_string(recordOffset) +
                          " implies a file of exactly " +
                          std::to_string(recordOffset + arrayBytes) + " bytes, but content is " +
                          std::to_string(content.size()) +
                          " bytes - spec Sec1 has this identity holding 96/96");
    }

    script.shots_.reserve(shotCount);
    for (uint16_t i = 0; i < shotCount; ++i) {
        size_t at = recordOffset + static_cast<size_t>(i) * kShotRecordSize;
        Shot shot;
        shot.offset = at;
        shot.startTime = readF32LE(content, at + 0x00);
        shot.endTime = readF32LE(content, at + 0x04);
        for (size_t ch = 0; ch < kChannelCount; ++ch) {
            shot.channels[ch] =
                readChannel(content, at + 0x08 + ch * kChannelRecordSize, ch, i);
        }
        script.shots_.push_back(std::move(shot));
    }

    return script;
}

std::vector<std::array<float, 3>> decodeVec3(const Channel& channel) {
    if (channel.valueWidth != 12) {
        throw FormatError("decodeVec3 called on channel " + std::to_string(channel.index) +
                          ", whose value width is " + std::to_string(channel.valueWidth) +
                          " bytes, not 12 - only channel 0 holds vec3 values (spec Sec5)");
    }
    // Drive the loop from the bytes actually held, NOT from keyCount: a
    // channel with a null values pointer (hasValues false) carries no
    // bytes even when keyCount is non-zero, and trusting keyCount there
    // would read off the end of the vector.
    size_t count = channel.valueBytes.size() / 12;
    std::vector<std::array<float, 3>> out;
    out.reserve(count);
    for (size_t k = 0; k < count; ++k) {
        size_t at = k * 12;
        std::array<float, 3> v{};
        for (size_t c = 0; c < 3; ++c) {
            uint32_t raw = 0;
            std::memcpy(&raw, channel.valueBytes.data() + at + c * 4, sizeof(raw));
            std::memcpy(&v[c], &raw, sizeof(float));
        }
        out.push_back(v);
    }
    return out;
}

std::vector<float> decodeFloats(const Channel& channel) {
    if (channel.valueWidth != 4) {
        // Refuses channel 1 (width 8): its elements are s16 quads (spec
        // Sec10.4), not floats - silently halving them into floats would
        // produce garbage that looks like data.
        throw FormatError("decodeFloats called on channel " + std::to_string(channel.index) +
                          ", whose value width is " + std::to_string(channel.valueWidth) +
                          " bytes, not 4 - channel 1's 8-byte elements are s16 quaternions "
                          "(spec Sec10.4): use decodeQuaternions/decodeQuaternionsRaw");
    }
    // Same guard as decodeVec3: count from the bytes held, not keyCount.
    size_t count = channel.valueBytes.size() / 4;
    std::vector<float> out;
    out.reserve(count);
    for (size_t k = 0; k < count; ++k) {
        uint32_t raw = 0;
        std::memcpy(&raw, channel.valueBytes.data() + k * 4, sizeof(raw));
        float value = 0.0f;
        std::memcpy(&value, &raw, sizeof(value));
        out.push_back(value);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Decoded values and the Sec10.3 sampler.
// ---------------------------------------------------------------------------

std::vector<std::array<int16_t, 4>> decodeQuaternionsRaw(const Channel& channel) {
    if (channel.valueWidth != 8) {
        throw FormatError("decodeQuaternionsRaw called on channel " +
                          std::to_string(channel.index) + ", whose value width is " +
                          std::to_string(channel.valueWidth) +
                          " bytes, not 8 - only channel 1 holds s16 quaternions (spec Sec10.4)");
    }
    size_t count = channel.valueBytes.size() / 8; // from the bytes held, not keyCount
    std::vector<std::array<int16_t, 4>> out;
    out.reserve(count);
    for (size_t k = 0; k < count; ++k) {
        std::array<int16_t, 4> q{};
        for (size_t c = 0; c < 4; ++c) q[c] = readS16(channel.valueBytes.data() + k * 8 + c * 2);
        out.push_back(q);
    }
    return out;
}

Quat quaternionFromRaw(const std::array<int16_t, 4>& raw) {
    Quat q;
    q.x = static_cast<double>(raw[0]) * kQuaternionScale;
    q.y = static_cast<double>(raw[1]) * kQuaternionScale;
    q.z = static_cast<double>(raw[2]) * kQuaternionScale;
    q.w = static_cast<double>(raw[3]) * kQuaternionScale;
    return q;
}

std::vector<Quat> decodeQuaternions(const Channel& channel) {
    std::vector<std::array<int16_t, 4>> raw = decodeQuaternionsRaw(channel);
    std::vector<Quat> out;
    out.reserve(raw.size());
    for (const auto& r : raw) out.push_back(quaternionFromRaw(r));
    return out;
}

std::array<std::array<double, 3>, 3> quaternionToMatrix(const Quat& q) {
    const double x = q.x, y = q.y, z = q.z, w = q.w;
    std::array<std::array<double, 3>, 3> m{};
    m[0] = {{1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - w * z), 2.0 * (x * z + w * y)}};
    m[1] = {{2.0 * (x * y + w * z), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - w * x)}};
    m[2] = {{2.0 * (x * z - w * y), 2.0 * (y * z + w * x), 1.0 - 2.0 * (x * x + y * y)}};
    return m;
}

Quat slerpShortestArc(const Quat& q0, const Quat& q1In, double f) {
    Quat q1 = q1In;
    double c = q0.x * q1.x + q0.y * q1.y + q0.z * q1.z + q0.w * q1.w;
    if (c < 0.0) {
        q1.x = -q1.x;
        q1.y = -q1.y;
        q1.z = -q1.z;
        q1.w = -q1.w;
        c = -c;
    }
    double w0 = 1.0 - f, w1 = f;
    if (1.0 - c > 1e-7) {
        const double theta = std::acos(c);
        const double s = std::sin(theta);
        w0 = std::sin((1.0 - f) * theta) / s;
        w1 = std::sin(f * theta) / s;
    }
    Quat r;
    r.x = w0 * q0.x + w1 * q1.x;
    r.y = w0 * q0.y + w1 * q1.y;
    r.z = w0 * q0.z + w1 * q1.z;
    r.w = w0 * q0.w + w1 * q1.w;
    return r;
}

namespace {

// Where t falls in a channel's key list, per Sec10.3 rules 1-4.
struct Locate {
    enum Kind { Empty, Single, Blend } kind = Empty;
    size_t a = 0; // Single: the key to return; Blend: the earlier key
    size_t b = 0; // Blend: the later key
    double f = 0.0;
};

size_t effectiveKeys(const Channel& c) {
    size_t n = c.keyCount;
    if (c.times.size() < n) n = c.times.size();
    size_t held = c.valueWidth ? c.valueBytes.size() / c.valueWidth : 0;
    if (held < n) n = held;
    return n;
}

Locate locate(const Channel& c, double t) {
    Locate r;
    const size_t n = effectiveKeys(c);
    if (n == 0) return r; // rule 1
    r.kind = Locate::Single;
    if (n == 1 || t <= static_cast<double>(c.times[0])) { // rule 2
        r.a = 0;
        return r;
    }
    if (t >= static_cast<double>(c.times[n - 1])) { // rule 3: hold, no extrapolation
        r.a = n - 1;
        return r;
    }
    size_t i = 1; // rule 4: first index with T[i] > t (t > T[0] here)
    while (i < n && !(static_cast<double>(c.times[i]) > t)) ++i;
    if (i >= n) { // only reachable with NaN times
        r.a = n - 1;
        return r;
    }
    const double d = static_cast<double>(c.times[i]) - static_cast<double>(c.times[i - 1]);
    if (!(d > 0.0)) { // d <= 0 -> V[i]
        r.a = i;
        return r;
    }
    r.kind = Locate::Blend;
    r.a = i - 1;
    r.b = i;
    r.f = (t - static_cast<double>(c.times[i - 1])) / d;
    return r;
}

void requireWidth(const Channel& c, size_t width, const char* fn) {
    if (c.valueWidth != width) {
        throw FormatError(std::string(fn) + " called on channel " + std::to_string(c.index) +
                          ", whose value width is " + std::to_string(c.valueWidth) +
                          " bytes, not " + std::to_string(width) + " (spec Sec10.3/10.5)");
    }
}

float f32At(const Channel& c, size_t key, size_t component) {
    float v = 0.0f;
    std::memcpy(&v, c.valueBytes.data() + key * c.valueWidth + component * 4, sizeof(v));
    return v;
}

} // namespace

double sampleScalarTrack(const Channel& channel, double t) {
    requireWidth(channel, 4, "sampleScalarTrack");
    const Locate loc = locate(channel, t);
    if (loc.kind == Locate::Empty) return 0.0;
    if (loc.kind == Locate::Single) return static_cast<double>(f32At(channel, loc.a, 0));
    const double v0 = f32At(channel, loc.a, 0), v1 = f32At(channel, loc.b, 0);
    return (1.0 - loc.f) * v0 + loc.f * v1;
}

std::array<double, 3> sampleVec3Track(const Channel& channel, double t) {
    requireWidth(channel, 12, "sampleVec3Track");
    const Locate loc = locate(channel, t);
    std::array<double, 3> out{};
    if (loc.kind == Locate::Empty) return out;
    for (size_t c = 0; c < 3; ++c) {
        if (loc.kind == Locate::Single) {
            out[c] = static_cast<double>(f32At(channel, loc.a, c));
        } else {
            const double v0 = f32At(channel, loc.a, c), v1 = f32At(channel, loc.b, c);
            out[c] = (1.0 - loc.f) * v0 + loc.f * v1;
        }
    }
    return out;
}

Quat sampleQuaternionTrack(const Channel& channel, double t) {
    requireWidth(channel, 8, "sampleQuaternionTrack");
    const Locate loc = locate(channel, t);
    if (loc.kind == Locate::Empty) return Quat{}; // identity
    auto keyAt = [&](size_t k) {
        std::array<int16_t, 4> raw{};
        for (size_t c = 0; c < 4; ++c) raw[c] = readS16(channel.valueBytes.data() + k * 8 + c * 2);
        return quaternionFromRaw(raw);
    };
    if (loc.kind == Locate::Single) return keyAt(loc.a);
    return slerpShortestArc(keyAt(loc.a), keyAt(loc.b), loc.f);
}

size_t CameraScript::shotIndexAt(double t) const {
    if (shots_.empty()) throw std::out_of_range("CameraScript::shotIndexAt on an empty script");
    for (size_t k = 0; k < shots_.size(); ++k) {
        if (t < static_cast<double>(shots_[k].endTime)) return k;
    }
    return shots_.size() - 1;
}

CameraSample CameraScript::sample(double t) const { return sampleShot(shotIndexAt(t), t); }

CameraSample CameraScript::sampleShot(size_t shotIndex, double t) const {
    if (shotIndex >= shots_.size()) {
        throw std::out_of_range("CameraScript::sampleShot: shot index " +
                                std::to_string(shotIndex) + " >= " +
                                std::to_string(shots_.size()) + " shots");
    }
    const Shot& shot = shots_[shotIndex];
    CameraSample out;
    out.shotIndex = shotIndex;
    out.position = sampleVec3Track(shot.channels[kChannelPosition], t);
    out.orientation = sampleQuaternionTrack(shot.channels[kChannelOrientation], t);
    out.fovRadians = sampleScalarTrack(shot.channels[kChannelFieldOfView], t);
    out.fovDegrees = out.fovRadians * kRadiansToDegrees;
    // Sec10.5: only channel 3's COUNT decides whether the group is enabled.
    out.dofEnabled = shot.channels[kChannelDofCocScale].keyCount != 0;
    if (out.dofEnabled) {
        out.dofCocScale = sampleScalarTrack(shot.channels[kChannelDofCocScale], t);
        for (size_t i = 0; i < 4; ++i) {
            out.dofFocalParams[i] =
                sampleScalarTrack(shot.channels[kChannelDofFocalFirst + i], t);
        }
    }
    return out;
}

} // namespace sr3cutscene
