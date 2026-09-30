// Synthetic tests for the .csc_pc cutscene-camera-script reader and its
// content validator. Builds buffers from scratch using only the CONFIRMED
// spec-cutscene-camera-format.md facts: version 4, the 12-byte header, the
// packed keyframe data region, the 0x68 shot record with its eight
// 12-byte channels, the per-channel value widths (12/8/4/4x5) and the
// exact-size identity `record_offset + count * 0x68 == size`.
//
// The builder lays the keyframe arrays out back-to-back with no padding,
// exactly as real files do - which matters because that packing is what
// let the spec derive the value widths arithmetically in the first place
// (Sec5), and a fixture with padding would not represent the format.

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "sr3cutscene/camera_script.h"
#include "sr3cutscene/content_validation.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":"          \
                      << __LINE__ << "\n";                                   \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

void appendU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void appendU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void appendF32(std::vector<uint8_t>& b, float v) {
    uint32_t raw;
    static_assert(sizeof(raw) == sizeof(v), "f32 must be 4 bytes");
    std::memcpy(&raw, &v, sizeof(raw));
    appendU32(b, raw);
}

void putU16At(std::vector<uint8_t>& b, size_t off, uint16_t v) {
    b[off + 0] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

void putU32At(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off + 0] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    b[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    b[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

// One channel's authored keyframes. `values` holds keyCount * width raw
// bytes; helpers below build them for the decodable widths.
struct SyntheticChannel {
    std::vector<float> times;
    std::vector<uint8_t> values;
};

SyntheticChannel vec3Channel(const std::vector<float>& times,
                              const std::vector<std::array<float, 3>>& points) {
    SyntheticChannel c;
    c.times = times;
    for (const auto& p : points) {
        for (float f : p) {
            uint32_t raw;
            std::memcpy(&raw, &f, sizeof(raw));
            c.values.push_back(static_cast<uint8_t>(raw & 0xFF));
            c.values.push_back(static_cast<uint8_t>((raw >> 8) & 0xFF));
            c.values.push_back(static_cast<uint8_t>((raw >> 16) & 0xFF));
            c.values.push_back(static_cast<uint8_t>((raw >> 24) & 0xFF));
        }
    }
    return c;
}

SyntheticChannel floatChannel(const std::vector<float>& times, const std::vector<float>& values) {
    SyntheticChannel c;
    c.times = times;
    for (float f : values) {
        uint32_t raw;
        std::memcpy(&raw, &f, sizeof(raw));
        c.values.push_back(static_cast<uint8_t>(raw & 0xFF));
        c.values.push_back(static_cast<uint8_t>((raw >> 8) & 0xFF));
        c.values.push_back(static_cast<uint8_t>((raw >> 16) & 0xFF));
        c.values.push_back(static_cast<uint8_t>((raw >> 24) & 0xFF));
    }
    return c;
}

// Channel 1: 8 bytes per key. Kept as an opaque-bytes builder (the reader
// still surfaces the raw bytes untouched) ...
SyntheticChannel opaque8Channel(const std::vector<float>& times,
                                 const std::vector<uint8_t>& rawBytes) {
    SyntheticChannel c;
    c.times = times;
    c.values = rawBytes;
    return c;
}

// ... and a builder that writes each key as four little-endian s16 (a,b,c,d)
// - the encoding spec Sec10.4 gives for channel 1 ((x,y,z,w) once scaled).
SyntheticChannel quatChannel(const std::vector<float>& times,
                             const std::vector<std::array<int16_t, 4>>& raws) {
    SyntheticChannel c;
    c.times = times;
    for (const auto& q : raws) {
        for (int16_t v : q) {
            const uint16_t u = static_cast<uint16_t>(v);
            c.values.push_back(static_cast<uint8_t>(u & 0xFF));
            c.values.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
        }
    }
    return c;
}

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

struct SyntheticShot {
    float startTime = 0.0f;
    float endTime = 0.0f;
    std::array<SyntheticChannel, sr3cutscene::kChannelCount> channels{};
};

// Builds a well-formed .csc_pc: 12-byte header, then the keyframe data
// region (every channel's times and values, packed back to back), then the
// shot-record array ending exactly at EOF.
//
// `spec12` selects the header the spec TEXT describes (12 bytes, a zero pad
// word at +0x08, keyframe data from +0x0C). With spec12 = false the pad word
// is omitted and the keyframe data starts at +0x08 - the layout measured on
// the shipped files (96/96: the first array is a times array at +0x08 whose
// first key, 0.0f, is the "zero pad"). The parser reads arrays through
// their pointers, so both parse.
std::vector<uint8_t> buildCsc(const std::vector<SyntheticShot>& shots, bool spec12 = true) {
    std::vector<uint8_t> b;

    appendU16(b, sr3cutscene::kRequiredVersion);
    appendU16(b, static_cast<uint16_t>(shots.size()));
    appendU32(b, 0); // record-array offset placeholder, patched below
    if (spec12) appendU32(b, 0); // +0x08 "padding" per the spec text

    // --- Keyframe data region, packed with no padding. Remember where
    // each array landed so the channel records can point at them. ---
    struct Placement {
        uint32_t timesOffset = 0;
        uint32_t valuesOffset = 0;
        uint32_t keyCount = 0;
    };
    std::vector<std::array<Placement, sr3cutscene::kChannelCount>> placements(shots.size());

    for (size_t s = 0; s < shots.size(); ++s) {
        for (size_t ch = 0; ch < sr3cutscene::kChannelCount; ++ch) {
            const SyntheticChannel& sc = shots[s].channels[ch];
            Placement p;
            p.keyCount = static_cast<uint32_t>(sc.times.size());
            p.timesOffset = static_cast<uint32_t>(b.size());
            for (float t : sc.times) appendF32(b, t);
            p.valuesOffset = static_cast<uint32_t>(b.size());
            b.insert(b.end(), sc.values.begin(), sc.values.end());

            // Fixture invariant: the value array must be exactly
            // keyCount * the width this channel index uses, or the
            // fixture is misbuilt rather than the reader being wrong.
            if (sc.values.size() != sc.times.size() * sr3cutscene::kChannelValueWidth[ch]) {
                std::cerr << "buildCsc: internal error, channel " << ch
                          << " value bytes != keyCount * width\n";
                std::abort();
            }
            placements[s][ch] = p;
        }
    }

    // --- Shot-record array: starts here, ends at EOF. ---
    uint32_t recordOffset = static_cast<uint32_t>(b.size());
    putU32At(b, 0x04, recordOffset);

    for (size_t s = 0; s < shots.size(); ++s) {
        size_t recStart = b.size();
        appendF32(b, shots[s].startTime);
        appendF32(b, shots[s].endTime);
        for (size_t ch = 0; ch < sr3cutscene::kChannelCount; ++ch) {
            appendU32(b, placements[s][ch].keyCount);
            appendU32(b, placements[s][ch].valuesOffset);
            appendU32(b, placements[s][ch].timesOffset);
        }
        if (b.size() - recStart != sr3cutscene::kShotRecordSize) {
            std::cerr << "buildCsc: internal error, shot record size != 0x68\n";
            std::abort();
        }
    }

    return b;
}

// A single shot with only the three always-present channels populated -
// the shape of the 396 shipped records where channels 3-7 are absent.
SyntheticShot basicShot(float start, float end) {
    SyntheticShot shot;
    shot.startTime = start;
    shot.endTime = end;
    shot.channels[sr3cutscene::kChannelPosition] =
        vec3Channel({start, end}, {{{1.0f, 2.0f, 3.0f}}, {{4.0f, 5.0f, 6.0f}}});
    shot.channels[sr3cutscene::kChannel1] =
        opaque8Channel({start}, {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04});
    shot.channels[sr3cutscene::kChannelFieldOfView] = floatChannel({start}, {0.691f});
    return shot;
}

bool throwsFormatError(const std::vector<uint8_t>& blob) {
    try {
        sr3cutscene::CameraScript::parse(sr3cutscene::ByteView(blob.data(), blob.size()));
    } catch (const sr3cutscene::FormatError&) {
        return true;
    }
    return false;
}

} // namespace

int main() {
    // --- Baseline: one shot, the three always-present channels. ---
    {
        std::vector<uint8_t> blob = buildCsc({basicShot(0.0f, 2.0f)});
        sr3cutscene::CameraScript s =
            sr3cutscene::CameraScript::parse(sr3cutscene::ByteView(blob.data(), blob.size()));

        CHECK(s.version() == 4);
        CHECK(s.shots().size() == 1);
        CHECK(s.field_0x08_raw() == 0);
        CHECK(s.shots()[0].startTime == 0.0f);
        CHECK(s.shots()[0].endTime == 2.0f);
        CHECK(s.duration() == 2.0f);

        const auto& pos = s.shots()[0].channels[sr3cutscene::kChannelPosition];
        CHECK(pos.keyCount == 2);
        CHECK(pos.valueWidth == 12);
        CHECK(pos.times.size() == 2);
        CHECK(pos.times[0] == 0.0f);
        CHECK(pos.times[1] == 2.0f);
        CHECK(pos.valueBytes.size() == 24);
        auto points = sr3cutscene::decodeVec3(pos);
        CHECK(points.size() == 2);
        CHECK(points[0][0] == 1.0f);
        CHECK(points[0][2] == 3.0f);
        CHECK(points[1][1] == 5.0f);

        const auto& fov = s.shots()[0].channels[sr3cutscene::kChannelFieldOfView];
        CHECK(fov.keyCount == 1);
        CHECK(fov.valueWidth == 4);
        auto fovValues = sr3cutscene::decodeFloats(fov);
        CHECK(fovValues.size() == 1);
        CHECK(fovValues[0] == 0.691f);

        // Channel 1's raw bytes are surfaced untouched...
        const auto& ch1 = s.shots()[0].channels[sr3cutscene::kChannel1];
        CHECK(ch1.valueWidth == 8);
        CHECK(ch1.valueBytes.size() == 8);
        CHECK(ch1.valueBytes[0] == 0xDE);
        CHECK(ch1.valueBytes[3] == 0xEF);
        // ...and decodeQuaternionsRaw reads them as four little-endian s16
        // (spec Sec10.4): DE AD | BE EF | 01 02 | 03 04.
        auto raw = sr3cutscene::decodeQuaternionsRaw(ch1);
        CHECK(raw.size() == 1);
        CHECK(raw[0][0] == static_cast<int16_t>(0xADDE));
        CHECK(raw[0][1] == static_cast<int16_t>(0xEFBE));
        CHECK(raw[0][2] == 0x0201);
        CHECK(raw[0][3] == 0x0403);
    }

    // --- Contiguity: shots tile the timeline end to end, which held
    // 1,309/1,309 in shipped files. The reader exposes the test rather
    // than enforcing it (it's an empirical property, not a parser rule). ---
    {
        std::vector<uint8_t> blob =
            buildCsc({basicShot(0.0f, 2.0f), basicShot(2.0f, 5.0f), basicShot(5.0f, 9.5f)});
        sr3cutscene::CameraScript s =
            sr3cutscene::CameraScript::parse(sr3cutscene::ByteView(blob.data(), blob.size()));
        CHECK(s.shots().size() == 3);
        CHECK(s.shots()[1].isContiguousWith(s.shots()[0]));
        CHECK(s.shots()[2].isContiguousWith(s.shots()[1]));
        CHECK(s.duration() == 9.5f);
    }

    // --- A non-contiguous file must still PARSE - the tiling is a
    // property of shipped data, not a rule the engine's parser enforces,
    // so rejecting it would invent a constraint the format doesn't have. ---
    {
        std::vector<uint8_t> blob = buildCsc({basicShot(0.0f, 2.0f), basicShot(3.0f, 5.0f)});
        sr3cutscene::CameraScript s =
            sr3cutscene::CameraScript::parse(sr3cutscene::ByteView(blob.data(), blob.size()));
        CHECK(s.shots().size() == 2);
        CHECK(!s.shots()[1].isContiguousWith(s.shots()[0])); // detected, not rejected
    }

    // --- The optional group: channels 3-7 populated together, the shape
    // of the 1,009/1,405 records that carry them. ---
    {
        SyntheticShot shot = basicShot(0.0f, 1.0f);
        for (size_t ch = sr3cutscene::kChannelOptionalGroupFirst;
             ch <= sr3cutscene::kChannelOptionalGroupLast; ++ch) {
            shot.channels[ch] = floatChannel({0.0f}, {static_cast<float>(ch) * 1.5f});
        }
        std::vector<uint8_t> blob = buildCsc({shot});
        sr3cutscene::CameraScript s =
            sr3cutscene::CameraScript::parse(sr3cutscene::ByteView(blob.data(), blob.size()));
        for (size_t ch = sr3cutscene::kChannelOptionalGroupFirst;
             ch <= sr3cutscene::kChannelOptionalGroupLast; ++ch) {
            const auto& c = s.shots()[0].channels[ch];
            CHECK(c.keyCount == 1);
            CHECK(c.valueWidth == 4);
            auto values = sr3cutscene::decodeFloats(c);
            CHECK(values.size() == 1);
            CHECK(values[0] == static_cast<float>(ch) * 1.5f);
        }
    }

    // --- An empty channel: keyCount 0 with both pointers aimed at the end
    // of the data region, which is how shipped files represent absence
    // (they never use the -1 null case). Must parse cleanly. ---
    {
        SyntheticShot shot = basicShot(0.0f, 1.0f);
        // channels 3-7 left default-constructed: no times, no values
        std::vector<uint8_t> blob = buildCsc({shot});
        sr3cutscene::CameraScript s =
            sr3cutscene::CameraScript::parse(sr3cutscene::ByteView(blob.data(), blob.size()));
        const auto& absent = s.shots()[0].channels[5];
        CHECK(absent.keyCount == 0);
        CHECK(absent.times.empty());
        CHECK(absent.valueBytes.empty());
        CHECK(absent.hasTimes);  // pointer present, just no keys
        CHECK(absent.hasValues);
    }

    // --- A null (-1) channel pointer: unobserved in shipped files
    // (0 of 22,480) but it IS the format's documented null idiom, so it
    // must be handled rather than crashing or reading garbage. ---
    {
        std::vector<uint8_t> blob = buildCsc({basicShot(0.0f, 1.0f)});
        sr3cutscene::CameraScript probe =
            sr3cutscene::CameraScript::parse(sr3cutscene::ByteView(blob.data(), blob.size()));
        // Null out channel 0's VALUES pointer in the record, leaving its
        // key count intact - the awkward case the decoders must survive.
        size_t ch0At = probe.shots()[0].offset + 0x08;
        putU32At(blob, ch0At + 0x04, sr3cutscene::kNullOffset);

        sr3cutscene::CameraScript s =
            sr3cutscene::CameraScript::parse(sr3cutscene::ByteView(blob.data(), blob.size()));
        const auto& pos = s.shots()[0].channels[sr3cutscene::kChannelPosition];
        CHECK(!pos.hasValues);
        CHECK(pos.keyCount == 2);       // count still says 2...
        CHECK(pos.valueBytes.empty());  // ...but no bytes were read
        CHECK(pos.times.size() == 2);   // times pointer was untouched
        // The decoder must count from the bytes held, not from keyCount,
        // or it would read off the end of an empty vector.
        auto points = sr3cutscene::decodeVec3(pos);
        CHECK(points.empty());
    }

    // --- decodeFloats must REFUSE channel 1 rather than halving its
    // 8-byte elements into floats: they are s16 quads (spec Sec10.4, which
    // superseded Sec5.1's "not a quaternion"), so a float decode would be
    // garbage that looks like data. Use decodeQuaternions. ---
    {
        std::vector<uint8_t> blob = buildCsc({basicShot(0.0f, 1.0f)});
        sr3cutscene::CameraScript s =
            sr3cutscene::CameraScript::parse(sr3cutscene::ByteView(blob.data(), blob.size()));
        const auto& ch1 = s.shots()[0].channels[sr3cutscene::kChannel1];
        bool threw = false;
        try {
            sr3cutscene::decodeFloats(ch1);
        } catch (const sr3cutscene::FormatError&) {
            threw = true;
        }
        CHECK(threw);

        // And decodeVec3 must refuse a 4-byte channel for the same reason.
        bool threw2 = false;
        try {
            sr3cutscene::decodeVec3(s.shots()[0].channels[sr3cutscene::kChannelFieldOfView]);
        } catch (const sr3cutscene::FormatError&) {
            threw2 = true;
        }
        CHECK(threw2);
    }

    // --- Negative: wrong version must be rejected. ---
    {
        std::vector<uint8_t> blob = buildCsc({basicShot(0.0f, 1.0f)});
        putU16At(blob, 0x00, 5);
        CHECK(throwsFormatError(blob));
    }

    // --- Negative: a null (-1) record-array offset must be rejected -
    // the real parser rejects the file outright (spec Sec3). ---
    {
        std::vector<uint8_t> blob = buildCsc({basicShot(0.0f, 1.0f)});
        putU32At(blob, 0x04, sr3cutscene::kNullOffset);
        CHECK(throwsFormatError(blob));
    }

    // --- Negative: the exact-size identity, from both sides. A trailing
    // byte past the record array breaks `record_offset + count*0x68 ==
    // size`... ---
    {
        std::vector<uint8_t> blob = buildCsc({basicShot(0.0f, 1.0f)});
        blob.push_back(0x00);
        CHECK(throwsFormatError(blob));
    }
    // --- ...and so does a count claiming more records than the file
    // holds. ---
    {
        std::vector<uint8_t> blob = buildCsc({basicShot(0.0f, 1.0f)});
        putU16At(blob, 0x02, 2);
        CHECK(throwsFormatError(blob));
    }

    // --- Negative: a channel pointing its time array past EOF must be
    // rejected rather than read out of bounds. ---
    {
        std::vector<uint8_t> blob = buildCsc({basicShot(0.0f, 1.0f)});
        sr3cutscene::CameraScript probe =
            sr3cutscene::CameraScript::parse(sr3cutscene::ByteView(blob.data(), blob.size()));
        size_t ch0At = probe.shots()[0].offset + 0x08;
        putU32At(blob, ch0At + 0x08, static_cast<uint32_t>(blob.size() - 2)); // times offset near EOF
        CHECK(throwsFormatError(blob));
    }

    // --- Negative: truncated header. ---
    {
        std::vector<uint8_t> blob = buildCsc({basicShot(0.0f, 1.0f)});
        blob.resize(8);
        CHECK(throwsFormatError(blob));
    }

    // --- Content validation: looksLikeCscFilename. ---
    CHECK(sr3cutscene::looksLikeCscFilename("01_in.csc_pc"));
    CHECK(sr3cutscene::looksLikeCscFilename("22_OUT2.CSC_PC")); // case-insensitive
    CHECK(!sr3cutscene::looksLikeCscFilename("01_in.gsc_pc"));  // spec Sec2: 0 exist
    CHECK(!sr3cutscene::looksLikeCscFilename("something.ctdg_pc"));
    CHECK(!sr3cutscene::looksLikeCscFilename("no_extension_at_all"));

    // --- validateCscContent: well-formed and malformed. ---
    {
        std::vector<uint8_t> blob = buildCsc({basicShot(0.0f, 1.0f)});
        auto v = sr3cutscene::validateCscContent(blob);
        CHECK(v.status == sr3cutscene::CscValidation::WellFormed);
    }
    {
        std::vector<uint8_t> notCsc = {0x00, 0x01, 0x02, 0x03};
        auto v = sr3cutscene::validateCscContent(notCsc);
        CHECK(v.status == sr3cutscene::CscValidation::NotWellFormed);
    }

    // --- refineWithCscValidation: PERMANENT NO-OP (HANDOFF.md §9.78 -
    // decompressEntry never produces OkUnconfirmedContent any more), passes
    // EVERY status through completely unchanged, including
    // OkUnconfirmedContent itself. ---
    {
        vpp::DecompressResult in;
        in.status = vpp::DecodeStatus::Ok;
        in.data = {0x00, 0x01, 0x02};
        vpp::DecompressResult out = sr3cutscene::refineWithCscValidation(in);
        CHECK(out.status == vpp::DecodeStatus::Ok); // untouched
    }
    {
        std::vector<uint8_t> blob = buildCsc({basicShot(0.0f, 1.0f)});
        vpp::DecompressResult in;
        in.status = vpp::DecodeStatus::OkUnconfirmedContent;
        in.data = blob;
        vpp::DecompressResult out = sr3cutscene::refineWithCscValidation(in);
        CHECK(out.status == vpp::DecodeStatus::OkUnconfirmedContent); // unchanged - no-op
        CHECK(out.data == blob);
    }
    {
        std::vector<uint8_t> notCsc = {0x00, 0x01, 0x02, 0x03};
        vpp::DecompressResult in;
        in.status = vpp::DecodeStatus::OkUnconfirmedContent;
        in.data = notCsc;
        vpp::DecompressResult out = sr3cutscene::refineWithCscValidation(in);
        CHECK(out.status == vpp::DecodeStatus::OkUnconfirmedContent); // unchanged - no-op, even for content that would have failed the old check
        CHECK(out.data == notCsc);
    }

    // =====================================================================
    // Decoded values and the sampler: everything below is built from the
    // TEXT of spec Sec10 (10.3 the sampling rule, 10.4 the channel-1
    // encoding, 10.5 the FOV / DOF outputs), not from game data.
    // =====================================================================
    namespace cs = sr3cutscene;
    auto parseBlob = [](const std::vector<uint8_t>& blob) {
        return cs::CameraScript::parse(cs::ByteView(blob.data(), blob.size()));
    };
    const double kPi = 3.14159265358979323846;

    // --- Sec10.4: the scale is the DOUBLE 16385/2^28 = 2^-14 * (1 + 2^-14),
    // so 1.0 ~ 16383 ((2^14-1)(2^14+1) = 2^28-1, exactly 1 - 2^-28). ---
    {
        CHECK(cs::kQuaternionScale == std::ldexp(16385.0, -28));
        CHECK(cs::kQuaternionScale == std::ldexp(1.0, -14) * (1.0 + std::ldexp(1.0, -14)));
        double radToDeg = cs::kRadiansToDegrees; // (non-const copy: keeps the constant-condition warning quiet)
        CHECK(radToDeg == 57.2957763671875); // Sec10.5

        // Component order (x, y, z, w) and the scale, on distinct values.
        cs::Quat q = cs::quaternionFromRaw({{1, 2, 3, 4}});
        CHECK(q.x == 1 * cs::kQuaternionScale);
        CHECK(q.y == 2 * cs::kQuaternionScale);
        CHECK(q.z == 3 * cs::kQuaternionScale);
        CHECK(q.w == 4 * cs::kQuaternionScale);
        // Signed, not unsigned.
        CHECK(cs::quaternionFromRaw({{-1, 0, 0, 0}}).x == -cs::kQuaternionScale);
        // 16383 -> exactly 1 - 2^-28.
        CHECK(cs::quaternionFromRaw({{0, 0, 0, 16383}}).w == 1.0 - std::ldexp(1.0, -28));
        // NOT 1/32767 (the old refuted reading): the same raw value would
        // be about half as large.
        CHECK(!near(cs::quaternionFromRaw({{0, 0, 0, 16383}}).w, 16383.0 / 32767.0, 0.1));
        // A unit quaternion quantised at this scale is unit within 2 LSB.
        const double h = std::sqrt(0.5);
        std::array<int16_t, 4> r90 = {{0, 0, static_cast<int16_t>(std::lround(h / cs::kQuaternionScale)),
                                        static_cast<int16_t>(std::lround(h / cs::kQuaternionScale))}};
        cs::Quat q90 = cs::quaternionFromRaw(r90);
        CHECK(near(std::sqrt(q90.x * q90.x + q90.y * q90.y + q90.z * q90.z + q90.w * q90.w), 1.0,
                   2 * cs::kQuaternionScale));

        // decodeQuaternions over a built channel: raw and decoded agree.
        std::vector<std::array<int16_t, 4>> raws = {{{1, -2, 3, 16383}}, {{-16383, 0, 5, 0}}};
        std::vector<uint8_t> blob = buildCsc({[&] {
            SyntheticShot s = basicShot(0.0f, 2.0f);
            s.channels[cs::kChannelOrientation] = quatChannel({0.0f, 2.0f}, raws);
            return s;
        }()});
        cs::CameraScript s = parseBlob(blob);
        const auto& ch = s.shots()[0].channels[cs::kChannelOrientation];
        auto rawBack = cs::decodeQuaternionsRaw(ch);
        auto decBack = cs::decodeQuaternions(ch);
        CHECK(rawBack.size() == 2 && decBack.size() == 2);
        CHECK(rawBack[0] == raws[0] && rawBack[1] == raws[1]);
        CHECK(decBack[0].x == cs::quaternionFromRaw(raws[0]).x);
        CHECK(decBack[1].x == -16383 * cs::kQuaternionScale);
        CHECK(decBack[1].z == 5 * cs::kQuaternionScale);

        // Wrong-width channels are refused by the quaternion decoders.
        bool threwQ = false;
        try { cs::decodeQuaternionsRaw(s.shots()[0].channels[cs::kChannelFieldOfView]); }
        catch (const cs::FormatError&) { threwQ = true; }
        CHECK(threwQ);
        bool threwS = false;
        try { cs::sampleQuaternionTrack(s.shots()[0].channels[cs::kChannelPosition], 0.0); }
        catch (const cs::FormatError&) { threwS = true; }
        CHECK(threwS);
    }

    // --- Sec10.4: the quaternion->matrix formula, written out. ---
    {
        auto I = cs::quaternionToMatrix(cs::Quat{0, 0, 0, 1});
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) CHECK(near(I[i][j], i == j ? 1.0 : 0.0, 1e-15));
        // 90 degrees about z: [[0,-1,0],[1,0,0],[0,0,1]] straight from
        // M01 = 2(xy - wz), M10 = 2(xy + wz).
        const double h = std::sqrt(0.5);
        auto Mz = cs::quaternionToMatrix(cs::Quat{0, 0, h, h});
        CHECK(near(Mz[0][0], 0, 1e-12) && near(Mz[0][1], -1, 1e-12) && near(Mz[0][2], 0, 1e-12));
        CHECK(near(Mz[1][0], 1, 1e-12) && near(Mz[1][1], 0, 1e-12) && near(Mz[1][2], 0, 1e-12));
        CHECK(near(Mz[2][0], 0, 1e-12) && near(Mz[2][1], 0, 1e-12) && near(Mz[2][2], 1, 1e-12));
        // 90 degrees about x: M12 = 2(yz - wx) = -1, M21 = 2(yz + wx) = +1.
        auto Mx = cs::quaternionToMatrix(cs::Quat{h, 0, 0, h});
        CHECK(near(Mx[1][2], -1, 1e-12) && near(Mx[2][1], 1, 1e-12) && near(Mx[0][0], 1, 1e-12));
        // 90 degrees about y: M02 = 2(xz + wy) = +1, M20 = 2(xz - wy) = -1.
        auto My = cs::quaternionToMatrix(cs::Quat{0, h, 0, h});
        CHECK(near(My[0][2], 1, 1e-12) && near(My[2][0], -1, 1e-12) && near(My[1][1], 1, 1e-12));
        // The rows of a unit quaternion's matrix are orthonormal.
        const double a = 0.1, b = -0.5, c = 0.3, d = std::sqrt(1 - a * a - b * b - c * c);
        auto M = cs::quaternionToMatrix(cs::Quat{a, b, c, d});
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                const double dotp = M[i][0] * M[j][0] + M[i][1] * M[j][1] + M[i][2] * M[j][2];
                CHECK(near(dotp, i == j ? 1.0 : 0.0, 1e-12));
            }
    }

    // --- Sec10.3 slerp, exactly as stated. ---
    {
        const double h = std::sqrt(0.5);
        cs::Quat id{0, 0, 0, 1}, z90{0, 0, h, h};
        // Midpoint of identity and 90 deg about z is 45 deg about z: (0,0,sin 22.5, cos 22.5).
        cs::Quat m = cs::slerpShortestArc(id, z90, 0.5);
        CHECK(near(m.z, std::sin(kPi / 8), 1e-12) && near(m.w, std::cos(kPi / 8), 1e-12));
        CHECK(near(m.x, 0, 1e-15) && near(m.y, 0, 1e-15));
        // f = 0 / 1 return the endpoints.
        cs::Quat e0 = cs::slerpShortestArc(id, z90, 0.0), e1 = cs::slerpShortestArc(id, z90, 1.0);
        CHECK(near(e0.w, 1, 1e-15) && near(e0.z, 0, 1e-15));
        CHECK(near(e1.w, h, 1e-12) && near(e1.z, h, 1e-12));
        // Shortest arc: q1 = -(10 deg about z). Without the negation the
        // blend of id and -q would pass through ~0; with it, the midpoint is
        // 5 deg about z, w ~ +cos 2.5deg and unit length.
        const double t10 = 10.0 * kPi / 180.0;
        cs::Quat neg{0, 0, -std::sin(t10 / 2), -std::cos(t10 / 2)};
        cs::Quat ms = cs::slerpShortestArc(id, neg, 0.5);
        CHECK(near(ms.w, std::cos(t10 / 4), 1e-12) && near(ms.z, std::sin(t10 / 4), 1e-12));
        // Near-identical (1 - c <= 1e-7) -> weights (1-f, f): the result is q.
        cs::Quat same = cs::slerpShortestArc(z90, z90, 0.37);
        CHECK(near(same.z, h, 1e-12) && near(same.w, h, 1e-12));
        // The blend is NOT renormalised: a chord-length check that the
        // spec's formula is what runs. For the 120-degree-apart pair the
        // result is exactly unit only because slerp preserves it.
        const double t120 = 120.0 * kPi / 180.0;
        cs::Quat z120{0, 0, std::sin(t120 / 2), std::cos(t120 / 2)};
        cs::Quat m120 = cs::slerpShortestArc(id, z120, 0.3);
        CHECK(near(std::sqrt(m120.z * m120.z + m120.w * m120.w), 1.0, 1e-12));
    }

    // --- Sec10.3 rules 1-4 on scalar / vec3 / quaternion tracks. ---
    {
        SyntheticShot sh = basicShot(0.0f, 10.0f);
        // ch2 (FOV) three keys: absolute times 0, 1, 2 with values 0, 10, 0
        sh.channels[cs::kChannelFieldOfView] = floatChannel({0.0f, 1.0f, 2.0f}, {0.0f, 10.0f, 0.0f});
        // ch0: two keys at absolute times 1 and 3
        sh.channels[cs::kChannelPosition] =
            vec3Channel({1.0f, 3.0f}, {{{10.0f, 0.0f, -4.0f}}, {{20.0f, 8.0f, -8.0f}}});
        // ch3 (DOF gate): a single key; ch4..7 left EMPTY (count 0)
        sh.channels[3] = floatChannel({0.0f}, {0.5f});
        std::vector<uint8_t> blob = buildCsc({sh});
        cs::CameraScript s = parseBlob(blob);
        const auto& fov = s.shots()[0].channels[cs::kChannelFieldOfView];
        // rule 4, several segments (the scan must pick the right one)
        CHECK(near(cs::sampleScalarTrack(fov, 0.25), 2.5, 1e-9));
        CHECK(near(cs::sampleScalarTrack(fov, 1.5), 5.0, 1e-9));
        CHECK(near(cs::sampleScalarTrack(fov, 1.75), 2.5, 1e-9));
        // exactly on an interior key: f = 0 in the following segment -> that key
        CHECK(cs::sampleScalarTrack(fov, 1.0) == 10.0);
        // rule 2: at/before the first key -> V[0]; rule 3: at/after the last -> V[n-1] (hold)
        CHECK(cs::sampleScalarTrack(fov, 0.0) == 0.0);
        CHECK(cs::sampleScalarTrack(fov, -100.0) == 0.0);
        CHECK(cs::sampleScalarTrack(fov, 2.0) == 0.0);
        CHECK(cs::sampleScalarTrack(fov, 1000.0) == 0.0);

        // vec3: componentwise lerp on ABSOLUTE times; hold before key 0 and after key 1
        const auto& pos = s.shots()[0].channels[cs::kChannelPosition];
        auto p = cs::sampleVec3Track(pos, 2.0); // midpoint of times 1 and 3
        CHECK(near(p[0], 15.0, 1e-9) && near(p[1], 4.0, 1e-9) && near(p[2], -6.0, 1e-9));
        p = cs::sampleVec3Track(pos, 1.5); // f = 0.25
        CHECK(near(p[0], 12.5, 1e-9) && near(p[1], 2.0, 1e-9) && near(p[2], -5.0, 1e-9));
        p = cs::sampleVec3Track(pos, 0.0); // before the first key (shot start 0 < key 1): hold V[0]
        CHECK(p[0] == 10.0 && p[1] == 0.0 && p[2] == -4.0);
        p = cs::sampleVec3Track(pos, 9.0); // shot tail: hold the last key
        CHECK(p[0] == 20.0 && p[1] == 8.0 && p[2] == -8.0);

        // rule 1: empty tracks -> 0 / zero vector / identity quaternion
        const auto& emptyCh = s.shots()[0].channels[5];
        CHECK(emptyCh.keyCount == 0);
        CHECK(cs::sampleScalarTrack(emptyCh, 1.0) == 0.0);
        // n = 1: V[0] for any t
        const auto& single = s.shots()[0].channels[3];
        CHECK(cs::sampleScalarTrack(single, -5.0) == 0.5 && cs::sampleScalarTrack(single, 123.0) == 0.5);

        // Whole-shot sample: FOV radians and its degrees (Sec10.5); DOF gate
        // is channel 3's COUNT, so channels 4-7 (count 0) sample as 0 (rule 1).
        cs::CameraSample cam = s.sampleShot(0, 1.5);
        CHECK(near(cam.fovRadians, 5.0, 1e-9));
        CHECK(near(cam.fovDegrees, 5.0 * 57.2957763671875, 1e-9));
        CHECK(cam.dofEnabled && cam.dofCocScale == 0.5);
        for (double v : cam.dofFocalParams) CHECK(v == 0.0);
        CHECK(cam.shotIndex == 0);
    }

    // --- Quaternion track sampler: keys at absolute times, hold, slerp. ---
    {
        const int16_t one = 16383;
        const int16_t s45 = static_cast<int16_t>(std::lround(std::sqrt(0.5) / cs::kQuaternionScale));
        SyntheticShot sh = basicShot(1.0f, 9.0f);
        sh.channels[cs::kChannelOrientation] =
            quatChannel({1.0f, 3.0f}, {{{0, 0, 0, one}}, {{0, 0, s45, s45}}});
        cs::CameraScript s = parseBlob(buildCsc({sh}));
        const auto& q = s.shots()[0].channels[cs::kChannelOrientation];
        cs::Quat a = cs::sampleQuaternionTrack(q, 1.0), b = cs::sampleQuaternionTrack(q, 3.0);
        CHECK(a.w == one * cs::kQuaternionScale && a.z == 0.0); // t <= T[0]
        CHECK(b.z == s45 * cs::kQuaternionScale);                // t >= T[n-1]
        cs::Quat h = cs::sampleQuaternionTrack(q, 2.0);          // f = 0.5, 45 deg about z
        CHECK(near(h.z, std::sin(kPi / 8), 5e-4) && near(h.w, std::cos(kPi / 8), 5e-4));
        // unit-length (not renormalised, but slerp keeps it unit to the stored precision)
        CHECK(near(std::sqrt(h.x * h.x + h.y * h.y + h.z * h.z + h.w * h.w), 1.0, 3e-4));
        cs::Quat late = cs::sampleQuaternionTrack(q, 500.0); // tail hold
        CHECK(late.z == b.z && late.w == b.w);
        cs::Quat early = cs::sampleQuaternionTrack(q, -500.0);
        CHECK(early.z == a.z && early.w == a.w);
    }

    // --- Null values pointer and empty quaternion track (rule 1): the
    // decoders/samplers count from the bytes held, not from keyCount. ---
    {
        std::vector<uint8_t> blob = buildCsc({basicShot(0.0f, 1.0f)});
        cs::CameraScript probe = parseBlob(blob);
        size_t ch1At = probe.shots()[0].offset + 0x08 + cs::kChannelOrientation * cs::kChannelRecordSize;
        putU32At(blob, ch1At + 0x04, cs::kNullOffset); // null the values pointer, keep the count
        cs::CameraScript s = parseBlob(blob);
        const auto& ch1 = s.shots()[0].channels[cs::kChannelOrientation];
        CHECK(!ch1.hasValues && ch1.keyCount == 1);
        CHECK(cs::decodeQuaternionsRaw(ch1).empty());
        cs::Quat idq = cs::sampleQuaternionTrack(ch1, 0.5); // effectively empty -> identity
        CHECK(idq.x == 0 && idq.y == 0 && idq.z == 0 && idq.w == 1);
        const auto& emptyQ = s.shots()[0].channels[6]; // an empty 4-byte channel
        CHECK(cs::sampleScalarTrack(emptyQ, 0.0) == 0.0);
    }

    // --- Timeline: absolute times, hard cuts at shot boundaries, the
    // clamp on the last shot (Sec10.6.1/10.6.4), no cross-shot blending. ---
    {
        auto mk = [&](float a, float b, float px) {
            SyntheticShot sh = basicShot(a, b);
            sh.channels[cs::kChannelPosition] =
                vec3Channel({a, b}, {{{px, 0.0f, 0.0f}}, {{px + 10.0f, 0.0f, 0.0f}}});
            return sh;
        };
        cs::CameraScript s = parseBlob(buildCsc({mk(0.0f, 2.0f, 0.0f), mk(2.0f, 5.0f, 100.0f), mk(5.0f, 9.5f, 200.0f)}));
        CHECK(s.shotIndexAt(-1.0) == 0);
        CHECK(s.shotIndexAt(0.0) == 0);
        CHECK(s.shotIndexAt(1.999) == 0);
        CHECK(s.shotIndexAt(2.0) == 1);   // exactly at the boundary: the NEXT shot
        CHECK(s.shotIndexAt(4.999) == 1);
        CHECK(s.shotIndexAt(5.0) == 2);
        CHECK(s.shotIndexAt(9.499) == 2);
        CHECK(s.shotIndexAt(9.5) == 2);   // clock clamps on the last shot
        CHECK(s.shotIndexAt(1000.0) == 2);
        // Hard cut: at t = 2.0 the position is shot 1's first key (100), not
        // a blend with shot 0's last key (10).
        CHECK(s.sample(2.0).position[0] == 100.0);
        CHECK(near(s.sample(1.999999).position[0], 10.0, 1e-3));
        // Keys are absolute: t = 3.5 is the midpoint of shot 1's [2,5] keys.
        CHECK(near(s.sample(3.5).position[0], 105.0, 1e-9));
        // After the last shot's end: last shot, last key held.
        CHECK(s.sample(50.0).shotIndex == 2 && s.sample(50.0).position[0] == 210.0);
        // sampleShot on a chosen shot ignores the shot boundaries (keys hold).
        CHECK(s.sampleShot(0, 3.5).position[0] == 10.0);
        bool threw = false;
        try { s.sampleShot(3, 0.0); } catch (const std::out_of_range&) { threw = true; }
        CHECK(threw);
        // A shot with no DOF channel 3 keys is not DOF-enabled.
        CHECK(!s.sample(1.0).dofEnabled && s.sample(1.0).dofCocScale == 0.0);
    }
    // an empty script: parses (count 0, record array at 12 == EOF) but cannot be sampled
    {
        std::vector<uint8_t> blob = buildCsc({});
        cs::CameraScript s = parseBlob(blob);
        CHECK(s.shots().empty());
        bool threw = false;
        try { s.shotIndexAt(0.0); } catch (const std::out_of_range&) { threw = true; }
        CHECK(threw);
    }

    // --- DOF group present in full (Sec10.5): channel 3 gate + 4-7 as Focal_params. ---
    {
        SyntheticShot sh = basicShot(0.0f, 4.0f);
        sh.channels[3] = floatChannel({0.0f, 4.0f}, {0.0f, 1.0f});
        sh.channels[4] = floatChannel({0.0f, 4.0f}, {1.0f, 5.0f});
        sh.channels[5] = floatChannel({0.0f, 4.0f}, {3.0f, 7.0f});
        sh.channels[6] = floatChannel({0.0f, 4.0f}, {10.0f, 30.0f});
        sh.channels[7] = floatChannel({0.0f, 4.0f}, {12.0f, 52.0f});
        cs::CameraScript s = parseBlob(buildCsc({sh}));
        cs::CameraSample cam = s.sample(2.0);
        CHECK(cam.dofEnabled);
        CHECK(near(cam.dofCocScale, 0.5, 1e-12));
        CHECK(near(cam.dofFocalParams[0], 3.0, 1e-12) && near(cam.dofFocalParams[1], 5.0, 1e-12));
        CHECK(near(cam.dofFocalParams[2], 20.0, 1e-12) && near(cam.dofFocalParams[3], 32.0, 1e-12));
    }

    // --- The MEASURED header layout (keyframe data from +0x08, no pad word):
    // parses identically, and the u32 at +0x08 is then just the first time
    // key. (The spec text says a 12-byte header; see camera_script.h
    // field_0x08_raw for the measurement that disagrees.) ---
    {
        SyntheticShot sh = basicShot(0.0f, 2.0f); // first array = ch0 times, first key 0.0f
        std::vector<uint8_t> blob = buildCsc({sh}, /*spec12=*/false);
        cs::CameraScript s = parseBlob(blob);
        CHECK(s.shots().size() == 1);
        CHECK(s.shots()[0].channels[cs::kChannelPosition].timesOffsetRaw == 8);
        CHECK(s.field_0x08_raw() == 0); // the bits of 0.0f
        CHECK(near(cs::sampleVec3Track(s.shots()[0].channels[0], 1.0)[0], 2.5, 1e-9));

        SyntheticShot sh2 = basicShot(1.0f, 2.0f); // first key 1.0f -> the word at +0x08 is 0x3F800000
        std::vector<uint8_t> blob2 = buildCsc({sh2}, false);
        cs::CameraScript s2 = parseBlob(blob2);
        CHECK(s2.field_0x08_raw() == 0x3F800000u);
    }

    if (g_failures == 0) {
        std::cout << "All synthetic csc/cutscene-format tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) FAILED.\n";
    return 1;
}
