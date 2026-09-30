// Synthetic tests for the .anim_pc keyframe payload reader
// (sr3anim/payload.h, spec-anim-format.md Sec6c).
//
// Fixtures are built from the SPEC, never from the reader. That rule earned
// its keep earlier in this project when three assertions in
// synthetic_ccmesh_test turned out to encode the reader's own pre-alignment
// as though it were a format fact - a test that agrees with the code it
// tests proves only that the code is self-consistent.
//
// Two fixtures are deliberately built from values this project did NOT
// choose: the worked rotation key supplied by Team A (control bytes
// 88 BF 7F C4, samples 00 6F C0, expected components +0.176798 / -0.013812
// / -0.022790). Those numbers came from an independent decoder, so they are
// an external oracle rather than a restatement of this reader's arithmetic -
// which matters, because getting exactly that assembly wrong is what
// produced a published-and-retracted refutation (HANDOFF Sec9.37.1).

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "sr3anim/animation.h"
#include "sr3anim/payload.h"

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

#define CHECK_NEAR(a, b, eps)                                                \
    do {                                                                     \
        const double d_ = std::fabs(static_cast<double>(a) -                 \
                                    static_cast<double>(b));                 \
        if (!(d_ <= (eps))) {                                                \
            std::cerr << "CHECK FAILED: " #a " ~= " #b " (got "              \
                      << (a) << " want " << (b) << ") at " __FILE__ ":"      \
                      << __LINE__ << "\n";                                   \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

void putU32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off + 0] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    b[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    b[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}
void putU16(std::vector<uint8_t>& b, size_t off, uint16_t v) {
    b[off + 0] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}
void putF32(std::vector<uint8_t>& b, size_t off, float v) {
    uint32_t raw = 0;
    std::memcpy(&raw, &v, sizeof(raw));
    putU32(b, off, raw);
}

// A minimal valid header per spec Sec2. `flags` bit 0x10 decides whether the
// header is 0x38 or 0x48 bytes, which is also where the payload begins when
// no track->bone table is present (Sec6b).
std::vector<uint8_t> makeHeader(uint8_t flags, uint8_t trackCount, uint8_t boneCount) {
    const size_t headerSize = (flags & 0x10) ? 0x48 : 0x38;
    std::vector<uint8_t> b(headerSize, 0);
    putU32(b, 0x00, 0x4D494E41u); // 'ANIM'
    b[0x04] = 14;                 // the only accepted version
    b[0x05] = flags;
    b[0x0A] = trackCount;
    b[0x0B] = boneCount;
    putF32(b, 0x0C, 0.0f);        // root rotation x,y,z,w
    putF32(b, 0x10, 0.0f);
    putF32(b, 0x14, 0.0f);
    putF32(b, 0x18, 1.0f);
    putF32(b, 0x1C, 0.0f);        // root translation
    putF32(b, 0x20, 0.0f);
    putF32(b, 0x24, 0.0f);
    putU32(b, 0x28, 0);           // no track->bone table
    putU32(b, 0x30, 0);           // declared payload end - patched by caller
    return b;
}

// One track block, narrow form (flags 0x80 clear, 0x20 clear), with a
// single rotation control record covering `rotKeys` and a single
// translation control record covering `transKeys`.
void appendTrack(std::vector<uint8_t>& b, uint8_t rotKeys, uint8_t transKeys,
                 const uint8_t rotCtl[4], const std::vector<uint8_t>& rotSamples,
                 int16_t tx, int16_t ty, int16_t tz,
                 const std::vector<uint8_t>& transSamples) {
    b.push_back(rotKeys);
    b.push_back(transKeys);

    if (rotKeys > 0) {
        for (int i = 0; i < 4; ++i) b.push_back(rotCtl[i]);
        for (uint8_t v : rotSamples) b.push_back(v);
    }

    // Sec6c.1: alignUp2 before the translation control records. Written out
    // here rather than calling a helper, so the fixture states the format
    // rather than mirroring the reader's implementation of it.
    while (b.size() % 2 != 0) b.push_back(0);

    if (transKeys > 0) {
        const size_t at = b.size();
        b.resize(at + 8, 0);
        putU16(b, at + 0, transKeys); // span covers every key
        putU16(b, at + 2, static_cast<uint16_t>(tx));
        putU16(b, at + 4, static_cast<uint16_t>(ty));
        putU16(b, at + 6, static_cast<uint16_t>(tz));
        for (uint8_t v : transSamples) b.push_back(v);
        // Sec6c.1: one further byte per translation key.
        for (uint8_t i = 0; i < transKeys; ++i) b.push_back(0);
    }
}

// ---------------------------------------------------------------------------

void testWalkAndEndpoint() {
    std::vector<uint8_t> b = makeHeader(0x09, 1, 1);
    const uint8_t ctl[4] = {0x00, 0x00, 0x00, 0x00}; // span = 1 key
    appendTrack(b, 1, 1, ctl, {0x00, 0x00, 0x00}, 0, 0, 0, {0x00, 0x00, 0x00});
    putU32(b, 0x30, static_cast<uint32_t>(b.size())); // the file declares its own end

    sr3anim::Animation a = sr3anim::Animation::parse(vpp::ByteView(b.data(), b.size()));
    sr3anim::Payload pl = sr3anim::Payload::walk(vpp::ByteView(b.data(), b.size()), a);

    CHECK(pl.walkComplete());
    CHECK(pl.landedOnDeclaredEnd());
    CHECK(pl.endCursor() == b.size());
    CHECK(pl.tracks().size() == 1);
    CHECK(pl.tracks()[0].rotationKeys == 1);
    CHECK(pl.tracks()[0].translationKeys == 1);
    CHECK(!pl.hasUnaccountedPayload());
}

void testEndpointMismatchIsReported() {
    std::vector<uint8_t> b = makeHeader(0x09, 1, 1);
    const uint8_t ctl[4] = {0, 0, 0, 0};
    appendTrack(b, 1, 1, ctl, {0, 0, 0}, 0, 0, 0, {0, 0, 0});
    putU32(b, 0x30, static_cast<uint32_t>(b.size() + 4)); // deliberately wrong

    sr3anim::Animation a = sr3anim::Animation::parse(vpp::ByteView(b.data(), b.size()));
    sr3anim::Payload pl = sr3anim::Payload::walk(vpp::ByteView(b.data(), b.size()), a);
    // The walk still completes; the ORACLE is what fails, and the reader
    // must distinguish those two things rather than conflating them.
    CHECK(pl.walkComplete());
    CHECK(!pl.landedOnDeclaredEnd());
}

void testTruncatedPayloadDoesNotThrow() {
    std::vector<uint8_t> b = makeHeader(0x09, 4, 4);
    const uint8_t ctl[4] = {0, 0, 0, 0};
    appendTrack(b, 1, 1, ctl, {0, 0, 0}, 0, 0, 0, {0, 0, 0});
    putU32(b, 0x30, static_cast<uint32_t>(b.size()));
    // Header declares 4 tracks; only one is present.

    sr3anim::Animation a = sr3anim::Animation::parse(vpp::ByteView(b.data(), b.size()));
    sr3anim::Payload pl = sr3anim::Payload::walk(vpp::ByteView(b.data(), b.size()), a);
    // Sec6c.4 makes incomplete walks a KNOWN population, so this must be a
    // reported state, not an exception and not a crash.
    CHECK(!pl.walkComplete());
    CHECK(!pl.landedOnDeclaredEnd());
}

void testExtraPayloadFlagIsSurfaced() {
    std::vector<uint8_t> b = makeHeader(0x49, 1, 1); // 0x40 set
    const uint8_t ctl[4] = {0, 0, 0, 0};
    appendTrack(b, 1, 1, ctl, {0, 0, 0}, 0, 0, 0, {0, 0, 0});
    putU32(b, 0x30, static_cast<uint32_t>(b.size()));

    sr3anim::Animation a = sr3anim::Animation::parse(vpp::ByteView(b.data(), b.size()));
    sr3anim::Payload pl = sr3anim::Payload::walk(vpp::ByteView(b.data(), b.size()), a);
    // 0/518 real clips with this bit land on their declared end, and what
    // the bit adds is OPEN - so the reader must say the clip is not fully
    // accounted for even when the declared tracks walk.
    CHECK(pl.hasUnaccountedPayload());
}

void testLongHeaderShiftsPayloadStart() {
    // flags bit 0x10 makes the header 0x48 rather than 0x38 (Sec2/Sec3).
    std::vector<uint8_t> b = makeHeader(0x19, 1, 1);
    CHECK(b.size() == 0x48);
    const uint8_t ctl[4] = {0, 0, 0, 0};
    appendTrack(b, 1, 1, ctl, {0, 0, 0}, 0, 0, 0, {0, 0, 0});
    putU32(b, 0x30, static_cast<uint32_t>(b.size()));

    sr3anim::Animation a = sr3anim::Animation::parse(vpp::ByteView(b.data(), b.size()));
    sr3anim::Payload pl = sr3anim::Payload::walk(vpp::ByteView(b.data(), b.size()), a);
    CHECK(pl.walkComplete());
    CHECK(pl.landedOnDeclaredEnd());
    CHECK(pl.tracks()[0].blockStart == 0x48);
}

void testTranslationDecode() {
    // Sec6c.5: axis = base/64 + delta/4000, base an i16 in the control
    // record, delta a signed byte per axis per key.
    std::vector<uint8_t> b = makeHeader(0x09, 1, 1);
    const uint8_t ctl[4] = {0, 0, 0, 0};
    // base (64, -128, 640) -> 1.0, -2.0, 10.0 before deltas.
    // deltas (+40, -40, 0)  -> +0.01, -0.01, 0.
    appendTrack(b, 1, 1, ctl, {0, 0, 0}, 64, -128, 640,
                {static_cast<uint8_t>(40), static_cast<uint8_t>(-40), 0});
    putU32(b, 0x30, static_cast<uint32_t>(b.size()));

    vpp::ByteView bv(b.data(), b.size());
    sr3anim::Animation a = sr3anim::Animation::parse(bv);
    sr3anim::Payload pl = sr3anim::Payload::walk(bv, a);
    CHECK(pl.walkComplete());

    const std::vector<sr3anim::Vec3> t = pl.translations(bv, 0);
    CHECK(t.size() == 1);
    if (t.size() == 1) {
        CHECK_NEAR(t[0].x, 1.0 + 40.0 / 4000.0, 1e-6);
        CHECK_NEAR(t[0].y, -2.0 - 40.0 / 4000.0, 1e-6);
        CHECK_NEAR(t[0].z, 10.0, 1e-6);
    }
}

void testRotationAgainstIndependentOracle() {
    // EXTERNAL ORACLE - these bytes and expected components come from Team
    // A's decoder, not from this one (HANDOFF Sec9.37.1). Control bytes
    // 88 BF 7F C4, sample bytes 00 6F C0:
    //   axis0 base=8  mult=4 delta=0  -> +0.176798
    //   axis1 base=-1 mult=4 delta=6  -> -0.013812
    //   axis2 base=-1 mult=2 delta=-1 -> -0.022790
    // permutation selector 3 -> row [0,1,2,3], reconstructed component last.
    std::vector<uint8_t> b = makeHeader(0x09, 1, 1);
    const uint8_t ctl[4] = {0x88, 0xBF, 0x7F, 0xC4}; // span = 1 + 0x04 = 5
    appendTrack(b, 1, 0, ctl, {0x00, 0x6F, 0xC0}, 0, 0, 0, {});
    putU32(b, 0x30, static_cast<uint32_t>(b.size()));

    vpp::ByteView bv(b.data(), b.size());
    sr3anim::Animation a = sr3anim::Animation::parse(bv);
    sr3anim::Payload pl = sr3anim::Payload::walk(bv, a);
    CHECK(pl.walkComplete());

    const std::vector<sr3anim::RotationSample> r = pl.rotations(bv, 0);
    CHECK(r.size() == 1);
    if (r.size() == 1) {
        CHECK_NEAR(r[0].value.x, 0.176798, 1e-5);
        CHECK_NEAR(r[0].value.y, -0.013812, 1e-5);
        CHECK_NEAR(r[0].value.z, -0.022790, 1e-5);
        CHECK_NEAR(r[0].value.w, 0.983886, 1e-5);
        CHECK(r[0].reconstructedLane == 3);
        CHECK(!r[0].clamped);
    }
}

void testRotationRangeReachesFullCircle() {
    // The bug that produced a retracted refutation capped components at
    // 0.181 and rotation at 36.5 degrees. This test fails if that ever
    // comes back: drive base and delta to their extremes and require a
    // component beyond anything the broken assembly could reach.
    std::vector<uint8_t> b = makeHeader(0x09, 1, 1);
    // base = -32 (0x20 sign-extends to -32), step index 3 -> mult 16.
    const uint8_t ctl[4] = {0xE0, 0x00, 0x00, 0xC0};
    appendTrack(b, 1, 0, ctl, {0x00, 0x00, 0x00}, 0, 0, 0, {});
    putU32(b, 0x30, static_cast<uint32_t>(b.size()));

    vpp::ByteView bv(b.data(), b.size());
    sr3anim::Animation a = sr3anim::Animation::parse(bv);
    sr3anim::Payload pl = sr3anim::Payload::walk(bv, a);
    const std::vector<sr3anim::RotationSample> r = pl.rotations(bv, 0);
    CHECK(r.size() == 1);
    if (r.size() == 1) {
        // base -32, mult 16, delta 0 -> (-32*64 + 0) * 4 * SCALE
        //   = -8192 * 8.6327287135645750e-05 = -0.70719329...
        // (The first version of this line said -0.707160, computed by hand
        // from a rounded scale. The test caught it, which is the point of
        // writing the expectation out rather than printing what the reader
        // returns and pasting it back in.)
        CHECK_NEAR(r[0].value.x, -0.70719329, 1e-6);
        // Decisive: the broken assembly could not exceed 0.181 in magnitude.
        CHECK(std::fabs(r[0].value.x) > 0.5);
    }
    CHECK(sr3anim::Payload::rotationsUsable());
}

void testSignedBaseBoundary() {
    // A 6-bit base is SIGNED: 0x1F is +31, 0x20 is -32. Reading it unsigned
    // was one of two defects in the first implementation here.
    std::vector<uint8_t> b = makeHeader(0x09, 1, 1);
    const uint8_t ctl[4] = {0x1F, 0x20, 0x00, 0xC0}; // +31, -32, 0; mult 1
    appendTrack(b, 1, 0, ctl, {0x00, 0x00, 0x00}, 0, 0, 0, {});
    putU32(b, 0x30, static_cast<uint32_t>(b.size()));

    vpp::ByteView bv(b.data(), b.size());
    sr3anim::Animation a = sr3anim::Animation::parse(bv);
    sr3anim::Payload pl = sr3anim::Payload::walk(bv, a);
    const std::vector<sr3anim::RotationSample> r = pl.rotations(bv, 0);
    CHECK(r.size() == 1);
    if (r.size() == 1) {
        CHECK(r[0].value.x > 0.0);  // +31
        CHECK(r[0].value.y < 0.0);  // -32, and it must be NEGATIVE
    }
}

void testWideCountsBranch() {
    // flags bit 0x80 widens the per-track counts to u16 (Sec6c.1). 53 real
    // clips use it, and reading it wrong cost 0% of that population until
    // the alignUp fix.
    std::vector<uint8_t> b = makeHeader(0x89, 1, 1);
    while (b.size() % 2 != 0) b.push_back(0);
    size_t at = b.size();
    b.resize(at + 4, 0);
    putU16(b, at + 0, 1); // rotKeys
    putU16(b, at + 2, 0); // transKeys
    const uint8_t ctl[4] = {0x00, 0x00, 0x00, 0x00};
    for (int i = 0; i < 4; ++i) b.push_back(ctl[i]);
    b.push_back(0); b.push_back(0); b.push_back(0); // one 3-byte sample
    while (b.size() % 2 != 0) b.push_back(0);
    putU32(b, 0x30, static_cast<uint32_t>(b.size()));

    vpp::ByteView bv(b.data(), b.size());
    sr3anim::Animation a = sr3anim::Animation::parse(bv);
    sr3anim::Payload pl = sr3anim::Payload::walk(bv, a);
    CHECK(pl.walkComplete());
    CHECK(pl.landedOnDeclaredEnd());
    CHECK(pl.tracks().size() == 1);
    if (pl.tracks().size() == 1) CHECK(pl.tracks()[0].rotationKeys == 1);
}

void testMultiKeyRunSpan() {
    // One control record covers a RUN of keys: span = 1 + (byte3 & 0x3F).
    // Every key in the run shares base and mult but has its own delta.
    std::vector<uint8_t> b = makeHeader(0x09, 1, 1);
    const uint8_t ctl[4] = {0x00, 0x00, 0x00, 0x02}; // span = 3
    appendTrack(b, 3, 0, ctl, {0, 0, 0, 0, 0, 0, 0, 0, 0}, 0, 0, 0, {});
    putU32(b, 0x30, static_cast<uint32_t>(b.size()));

    vpp::ByteView bv(b.data(), b.size());
    sr3anim::Animation a = sr3anim::Animation::parse(bv);
    sr3anim::Payload pl = sr3anim::Payload::walk(bv, a);
    CHECK(pl.walkComplete());
    CHECK(pl.tracks()[0].rotationControlCount == 1);
    CHECK(pl.rotations(bv, 0).size() == 3);
}

void run(const char* name, void (*fn)()) {
    const int before = g_failures;
    try {
        fn();
    } catch (const std::exception& e) {
        std::cerr << "CHECK FAILED: " << name << " threw: " << e.what() << "\n";
        ++g_failures;
        return;
    }
    if (g_failures != before) std::cerr << "  (in " << name << ")\n";
}

} // namespace

int main() {
    run("walk and endpoint", testWalkAndEndpoint);
    run("endpoint mismatch reported", testEndpointMismatchIsReported);
    run("truncated payload does not throw", testTruncatedPayloadDoesNotThrow);
    run("extra-payload flag surfaced", testExtraPayloadFlagIsSurfaced);
    run("long header shifts payload start", testLongHeaderShiftsPayloadStart);
    run("translation decode", testTranslationDecode);
    run("rotation against independent oracle", testRotationAgainstIndependentOracle);
    run("rotation reaches full range", testRotationRangeReachesFullCircle);
    run("signed base boundary", testSignedBaseBoundary);
    run("wide counts branch", testWideCountsBranch);
    run("multi-key run span", testMultiKeyRunSpan);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic anim-payload tests passed.\n";
    return 0;
}
