// Synthetic tests for cutscene_play_do's real camera-script pipeline
// (spec-lua-api-behaviour.md Sec57.2a / spec-cutscene-camera-format.md
// Sec10.6 item 7, 2026-10-08 wiring task): the two pieces that already
// existed separately - the real .csc_pc reader/sampler
// (sr3cutscene::camera_script, tests/synthetic_csc_test.cpp) and the
// cutscene_play_do bookkeeping stub (src/lua_cutscene.cpp,
// tests/synthetic_luahost_tranche_realhits_test.cpp) - wired together end
// to end: name -> kind lookup -> (Designer: skip | Zscene/Story: real
// .csc_pc bytes -> sr3cutscene::CameraScript -> sampled pose).
//
// No real .csc_pc fixture exists anywhere in this project's test/golden
// data (grepped directly: zero `*.csc_pc` files in the repo, and
// tests/synthetic_csc_test.cpp's own fixtures are hand-built from the
// CONFIRMED spec text, not captured game data) - so this test follows that
// exact same, already-established house convention: a byte-for-byte
// correct .csc_pc built from spec-cutscene-camera-format.md's own
// confirmed facts (version 4, the measured 8-byte header, the packed
// keyframe region, the 0x68 shot record), not invented bytes. The builder
// below is a trimmed duplicate of tests/synthetic_csc_test.cpp's own
// buildCsc (one shot only) - duplicated with attribution rather than
// shared, the same convention tools/lua_host_run.cpp's own top comment
// documents for walk().
//
// What each test proves:
//  - testZsceneLoadsAndSamples / testStoryLoadsAndSamples: a Zscene/Story-
//    kind cutscene_play_do call results in the installed opener being
//    called exactly once, a REAL CameraScript being parsed, and real
//    sampled values (not just "didn't crash") at several time points -
//    lerp midpoint, hold-before-first-key, hold-after-last-key, and a
//    single-key FOV track, cross-checked against the Sec10.3 formulas
//    directly (same style as synthetic_csc_test.cpp's own checks).
//  - testDesignerSkipsEntirely: a Designer-kind call never invokes the
//    opener at all (both EngineState's own counter AND an independent
//    counter inside the opener lambda itself stay at 0) - "confirm no
//    .csc_pc read was attempted," per this task's own instruction #5.
//  - testKindUnknownNeverGuesses: the pre-existing "no scene table
//    installed" path (tests/synthetic_luahost_tranche_realhits_test.cpp's
//    own fixture shape) must keep producing ZERO opener calls - the CHOSEN
//    conservative default, and a direct regression guard for that
//    existing test's own behaviour.
//  - testMissingAndMalformedAbort: REAL per spec-cutscene-camera-format.md
//    Sec10.6 item 7 ("a missing or unparsable script aborts the cutscene
//    load") - a filename the opener can't find, and bytes the real
//    sr3cutscene reader rejects, both land on the SAME Aborted outcome,
//    with no CameraScript installed afterward.

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"

using namespace sr3luahost;

namespace {

int g_checks = 0;
int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        ++g_checks;                                                                           \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

std::vector<RegisteredName> names() { return {{"cutscene_play_do", "gameplay"}}; }

void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "cutscene_camera_script_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

// --- trimmed duplicate of tests/synthetic_csc_test.cpp's own buildCsc,
// one shot only (see file header for why this is duplicated, not shared). ---

void appendU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}
void appendU32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}
void appendF32(std::vector<uint8_t>& b, float v) {
    uint32_t raw;
    std::memcpy(&raw, &v, sizeof(raw));
    appendU32(b, raw);
}
void putU32At(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[off + i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
}

struct FixtureChannel {
    std::vector<float> times;
    std::vector<uint8_t> values;
};

FixtureChannel vec3Channel(const std::vector<float>& times, const std::vector<std::array<float, 3>>& pts) {
    FixtureChannel c;
    c.times = times;
    for (const auto& p : pts)
        for (float f : p) appendF32(c.values, f);
    return c;
}
// appendF32 above takes a vector<uint8_t>&; reuse it directly for values too.
FixtureChannel floatChannel(const std::vector<float>& times, const std::vector<float>& vals) {
    FixtureChannel c;
    c.times = times;
    for (float f : vals) appendF32(c.values, f);
    return c;
}

// version 4, the MEASURED 8-byte header (keyframe data from +0x08, no pad
// word - spec-cutscene-camera-format.md Sec3; both header shapes parse
// identically), one shot record, channels packed back to back with no
// padding (required: it's how the format's own per-channel widths were
// derived, spec Sec5).
std::vector<uint8_t> buildOneShotCsc(float start, float end,
                                      const std::array<FixtureChannel, sr3cutscene::kChannelCount>& channels) {
    std::vector<uint8_t> b;
    appendU16(b, sr3cutscene::kRequiredVersion);
    appendU16(b, 1); // one shot
    appendU32(b, 0); // record-array offset placeholder, patched below

    struct Placement {
        uint32_t timesOffset = 0, valuesOffset = 0, keyCount = 0;
    };
    std::array<Placement, sr3cutscene::kChannelCount> placements{};
    for (size_t ch = 0; ch < sr3cutscene::kChannelCount; ++ch) {
        placements[ch].keyCount = static_cast<uint32_t>(channels[ch].times.size());
        placements[ch].timesOffset = static_cast<uint32_t>(b.size());
        for (float t : channels[ch].times) appendF32(b, t);
        placements[ch].valuesOffset = static_cast<uint32_t>(b.size());
        b.insert(b.end(), channels[ch].values.begin(), channels[ch].values.end());
    }

    const uint32_t recordOffset = static_cast<uint32_t>(b.size());
    putU32At(b, 0x04, recordOffset);
    appendF32(b, start);
    appendF32(b, end);
    for (size_t ch = 0; ch < sr3cutscene::kChannelCount; ++ch) {
        appendU32(b, placements[ch].keyCount);
        appendU32(b, placements[ch].valuesOffset);
        appendU32(b, placements[ch].timesOffset);
    }
    return b;
}

// A real, format-correct .csc_pc: channel 0 (position) lerps from `p0` to
// `p1` over [start, end]; channel 2 (FOV) is a single key (n = 1: holds for
// any t, Sec10.3 rule 2/3); channels 1 and 3-7 are left empty.
std::vector<uint8_t> buildPositionFovCsc(float start, float end, std::array<float, 3> p0, std::array<float, 3> p1,
                                          float fovRadians) {
    std::array<FixtureChannel, sr3cutscene::kChannelCount> channels{};
    channels[sr3cutscene::kChannelPosition] = vec3Channel({start, end}, {p0, p1});
    channels[sr3cutscene::kChannelFieldOfView] = floatChannel({start}, {fovRadians});
    return buildOneShotCsc(start, end, channels);
}

EngineState::ZsceneTableRow makeRow(const std::string& name, std::optional<int> kind,
                                     std::optional<std::string> cameraScriptName) {
    EngineState::ZsceneTableRow row;
    row.name = name;
    row.crc = sr3save::nameHash(name);
    row.kind = kind;
    row.cameraScriptName = std::move(cameraScriptName);
    return row;
}

} // namespace

// Zscene (kind 1): real load + real sampled values.
void testZsceneLoadsAndSamples() {
    Host host(names());
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();

    es.installZsceneTable({makeRow("z01", 1, std::string("z01.csc_pc"))});

    int openerCallsLocal = 0;
    const std::vector<uint8_t> blob =
        buildPositionFovCsc(0.0f, 4.0f, {0.0f, 0.0f, 0.0f}, {40.0f, 80.0f, 0.0f}, 0.6981317f);
    es.installCameraScriptOpener([&](const std::string& fileName) -> std::optional<std::vector<uint8_t>> {
        ++openerCallsLocal;
        if (fileName != "z01.csc_pc") return std::nullopt;
        return blob;
    });

    CHECK(es.cutsceneCameraScriptOpenerCalls() == 0);
    ok(host, gp, "cutscene_play_do('z01', nil, {'a', 'b'})");

    CHECK(es.cutscenePlayInProgress());
    CHECK(openerCallsLocal == 1);
    CHECK(es.cutsceneCameraScriptOpenerCalls() == 1);
    CHECK(es.cutsceneCameraScriptOutcome() == EngineState::CutsceneCameraScriptOutcome::Loaded);
    CHECK(es.cutsceneActiveCameraScript() != nullptr);

    // Real sampled values, cross-checked against the Sec10.3 rules
    // directly (not just "didn't crash"):
    const sr3cutscene::CameraSample s0 = es.sampleCutsceneCamera(0.0);
    CHECK(s0.position[0] == 0.0 && s0.position[1] == 0.0 && s0.position[2] == 0.0);
    const sr3cutscene::CameraSample s2 = es.sampleCutsceneCamera(2.0); // lerp midpoint
    CHECK(near(s2.position[0], 20.0, 1e-6) && near(s2.position[1], 40.0, 1e-6));
    const sr3cutscene::CameraSample s4 = es.sampleCutsceneCamera(4.0); // last key
    CHECK(near(s4.position[0], 40.0, 1e-6) && near(s4.position[1], 80.0, 1e-6));
    const sr3cutscene::CameraSample sTail = es.sampleCutsceneCamera(1000.0); // tail hold (Sec10.3 rule 3)
    CHECK(near(sTail.position[0], 40.0, 1e-6) && near(sTail.position[1], 80.0, 1e-6));
    const sr3cutscene::CameraSample sBefore = es.sampleCutsceneCamera(-5.0); // hold before first key (rule 2)
    CHECK(sBefore.position[0] == 0.0 && sBefore.position[1] == 0.0);
    // Single-key FOV track: holds for ANY t (Sec10.3: n = 1 -> V[0]). The
    // stored value went through a float32 round-trip (the file format's own
    // element width, Sec3), so the expected value is computed through that
    // SAME round-trip rather than compared against the literal double -
    // otherwise float32(0.6981317) vs double 0.6981317 differ by ~4e-8,
    // which Sec10.5's x57.2957... degrees conversion amplifies past a
    // naive 1e-6 tolerance.
    const double expectedFovRadA = static_cast<double>(0.6981317f);
    CHECK(near(s0.fovRadians, expectedFovRadA, 1e-9));
    CHECK(near(s2.fovRadians, expectedFovRadA, 1e-9));
    CHECK(near(s0.fovDegrees, expectedFovRadA * sr3cutscene::kRadiansToDegrees, 1e-9));
    // Channel 1 was never populated: identity orientation (Sec10.3 rule 1).
    CHECK(s2.orientation.x == 0.0 && s2.orientation.y == 0.0 && s2.orientation.z == 0.0 && s2.orientation.w == 1.0);
    // No DOF group (channel 3 absent).
    CHECK(!s2.dofEnabled);

    // A second start is refused (one-way "in progress" latch, Sec57.2) -
    // the camera-script step must not run again / must not change outcome.
    ok(host, gp, "cutscene_play_do('z01', nil, {'c', 'd'})");
    CHECK(openerCallsLocal == 1);
    CHECK(es.cutsceneCameraScriptOpenerCalls() == 1);
}

// Story (kind 2): same pipeline, distinct data, proving this isn't special-
// cased to kind 1.
void testStoryLoadsAndSamples() {
    Host host(names());
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();

    es.installZsceneTable({makeRow("s01", 2, std::string("s01.csc_pc"))});
    const std::vector<uint8_t> blob =
        buildPositionFovCsc(0.0f, 2.0f, {100.0f, 0.0f, 0.0f}, {300.0f, 0.0f, 0.0f}, 1.0471976f);
    int openerCallsLocal = 0;
    es.installCameraScriptOpener([&](const std::string& fileName) -> std::optional<std::vector<uint8_t>> {
        ++openerCallsLocal;
        CHECK(fileName == "s01.csc_pc");
        return blob;
    });

    ok(host, gp, "cutscene_play_do('s01', nil, {'a', 'b'})");

    CHECK(openerCallsLocal == 1);
    CHECK(es.cutsceneCameraScriptOutcome() == EngineState::CutsceneCameraScriptOutcome::Loaded);
    const sr3cutscene::CameraSample mid = es.sampleCutsceneCamera(1.0); // lerp midpoint of [0,2]
    CHECK(near(mid.position[0], 200.0, 1e-6) && mid.position[1] == 0.0);
    // Same float32-round-trip reasoning as testZsceneLoadsAndSamples above.
    const double expectedFovRadS = static_cast<double>(1.0471976f);
    CHECK(near(mid.fovDegrees, expectedFovRadS * sr3cutscene::kRadiansToDegrees, 1e-9));
}

// Designer (kind 0): the 0x00721bc0 gate skips the load ENTIRELY - the
// opener must never be invoked (instruction #5's own explicit ask).
void testDesignerSkipsEntirely() {
    Host host(names());
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();

    es.installZsceneTable({makeRow("d01", 0, std::string("d01.csc_pc"))}); // even with a name present
    int openerCallsLocal = 0;
    es.installCameraScriptOpener([&](const std::string&) -> std::optional<std::vector<uint8_t>> {
        ++openerCallsLocal; // must stay 0 - a Designer cutscene never attempts a .csc_pc read
        return std::nullopt;
    });

    ok(host, gp, "cutscene_play_do('d01', nil, {'a', 'b'})");

    CHECK(es.cutscenePlayInProgress()); // the start itself still succeeds
    CHECK(openerCallsLocal == 0);       // independent counter inside the opener body itself
    CHECK(es.cutsceneCameraScriptOpenerCalls() == 0); // EngineState's own counter agrees
    CHECK(es.cutsceneCameraScriptOutcome() == EngineState::CutsceneCameraScriptOutcome::SkippedDesignerKind);
    CHECK(es.cutsceneActiveCameraScript() == nullptr);
    bool threw = false;
    try {
        es.sampleCutsceneCamera(0.0);
    } catch (const std::logic_error&) {
        threw = true;
    }
    CHECK(threw);
}

// No scene table installed (the exact fixture shape
// tests/synthetic_luahost_tranche_realhits_test.cpp's own cutscene_play_do
// coverage already uses and depends on NOT throwing/NOT changing
// behaviour): kind is categorically undeterminable, so the camera-script
// step must be a conservative, silent no-op - a direct regression guard.
void testKindUnknownNeverGuesses() {
    Host host(names());
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();

    es.zsceneLoadable().set("unk01", true); // the OPEN-until-set per-name map, no table installed
    int openerCallsLocal = 0;
    es.installCameraScriptOpener([&](const std::string&) -> std::optional<std::vector<uint8_t>> {
        ++openerCallsLocal;
        return std::nullopt;
    });

    ok(host, gp, "cutscene_play_do('unk01', nil, {'a', 'b'})");

    CHECK(es.cutscenePlayInProgress()); // unchanged pre-existing behaviour
    CHECK(openerCallsLocal == 0);
    CHECK(es.cutsceneCameraScriptOpenerCalls() == 0);
    CHECK(es.cutsceneCameraScriptOutcome() == EngineState::CutsceneCameraScriptOutcome::SkippedKindUnknown);
    CHECK(es.cutsceneActiveCameraScript() == nullptr);
}

// REAL (spec-cutscene-camera-format.md Sec10.6 item 7): "a missing or
// unparsable script aborts the cutscene load" - both a name the opener
// can't find and bytes the real reader rejects land on the same Aborted
// outcome, never a crash and never a silently-ignored "load."
void testMissingAndMalformedAbort() {
    {
        Host host(names());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.installZsceneTable({makeRow("miss01", 1, std::string("missing.csc_pc"))});
        int openerCallsLocal = 0;
        es.installCameraScriptOpener([&](const std::string& fileName) -> std::optional<std::vector<uint8_t>> {
            ++openerCallsLocal;
            CHECK(fileName == "missing.csc_pc");
            return std::nullopt; // not found
        });
        ok(host, gp, "cutscene_play_do('miss01', nil, {'a', 'b'})");
        CHECK(openerCallsLocal == 1);
        CHECK(es.cutsceneCameraScriptOutcome() == EngineState::CutsceneCameraScriptOutcome::Aborted);
        CHECK(es.cutsceneActiveCameraScript() == nullptr);
    }
    {
        Host host(names());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.installZsceneTable({makeRow("bad01", 2, std::string("bad.csc_pc"))});
        const std::vector<uint8_t> garbage = {0x00, 0x01, 0x02, 0x03}; // wrong version / truncated
        int openerCallsLocal = 0;
        es.installCameraScriptOpener([&](const std::string&) -> std::optional<std::vector<uint8_t>> {
            ++openerCallsLocal;
            return garbage;
        });
        ok(host, gp, "cutscene_play_do('bad01', nil, {'a', 'b'})");
        CHECK(openerCallsLocal == 1);
        CHECK(es.cutsceneCameraScriptOutcome() == EngineState::CutsceneCameraScriptOutcome::Aborted);
        CHECK(es.cutsceneActiveCameraScript() == nullptr);
    }
    {
        // No opener installed at all (a host never wired to a byte source):
        // same Aborted outcome, not a crash/null-deref.
        Host host(names());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.installZsceneTable({makeRow("noopener01", 1, std::string("whatever.csc_pc"))});
        ok(host, gp, "cutscene_play_do('noopener01', nil, {'a', 'b'})");
        CHECK(es.cutsceneCameraScriptOpenerCalls() == 0); // nothing to call
        CHECK(es.cutsceneCameraScriptOutcome() == EngineState::CutsceneCameraScriptOutcome::Aborted);
    }
}

int main() {
    testZsceneLoadsAndSamples();
    testStoryLoadsAndSamples();
    testDesignerSkipsEntirely();
    testKindUnknownNeverGuesses();
    testMissingAndMalformedAbort();
    std::cout << "cutscene_play_do camera-script pipeline: " << g_checks << " checks, " << g_failures
              << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
