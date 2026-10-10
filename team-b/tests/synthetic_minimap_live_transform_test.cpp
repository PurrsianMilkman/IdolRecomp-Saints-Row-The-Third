// Synthetic tests (2026-10-08, minimap live-transform task) for
// include/sr3luahost/minimap_live_transform.h - spec-hud-runtime.md Sec6/
// Sec6.4's CONFIRMED-mechanism corner-minimap background transform
// (0x00809910): a live map-centre that tracks the player's own world
// position every tick (falling back to a linked vehicle's position when
// seated), and a background rotation gated by the save-format "static
// minimap" option (spec-save-format.md Sec7.5). Also covers sr2_map's own
// 2 real properties (zoom/map_mode) through the EXISTING typed VINT
// property store (EngineState::setVintProperty/getVintProperty), and the
// player marker's own fixed-screen-position invariant (Sec6.4's single
// most important confirmed behavioural fact). No game data.
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/host.h"
#include "sr3luahost/minimap_live_transform.h"
#include "sr3luahost/vint_property_types.h"

using namespace sr3luahost;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (0)

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

// Same helper as tests/synthetic_vint_property_test.cpp's own (mirrored,
// not shared, since that file owns no header of its own to include from).
template <typename F>
bool throwsOpen(F f, const char* needle = nullptr) {
    try {
        f();
    } catch (const OpenStateError& e) {
        if (needle && std::string(e.what()).find(needle) == std::string::npos) {
            std::cerr << "  (OpenStateError without '" << needle << "': " << e.what() << ")\n";
            return false;
        }
        return true;
    }
    return false;
}

// This project's own CHOSEN demo-level world->minimap-screen convention
// (tools/sr3_viewer.cpp's runMinimapDemo() uses the identical formula): a
// heading-up rotation (world rotated by -heading so the player's forward
// direction stays pointing the same way on screen) around a fixed screen
// centre, scaled by a caller-supplied pixels-per-world-unit factor. Mirrored
// here (not included from the tool, which also owns D3D/window code) so
// this test can prove the player's OWN marker position - computed by
// feeding the world point EQUAL TO the live map-centre itself through this
// same formula - never moves, which is the end-to-end proof that matters
// (not just that MinimapLiveTransform's own output struct has no player-
// position field).
struct ScreenPoint { double x, y; };
ScreenPoint worldToMinimapScreen(double wx, double wz, const MinimapLiveTransform& t, double screenCentreX,
                                  double screenCentreY, double pixelsPerWorldUnit) {
    const double rx = wx - t.mapCentreX, rz = wz - t.mapCentreZ;
    const double c = std::cos(-t.backgroundRotationRad), s = std::sin(-t.backgroundRotationRad);
    const double rotX = rx * c - rz * s, rotZ = rx * s + rz * c;
    return ScreenPoint{screenCentreX + rotX * pixelsPerWorldUnit, screenCentreY + rotZ * pixelsPerWorldUnit};
}

} // namespace

int main() {
    // --- 1. map-centre tracks the player's own moving position every tick,
    // on foot (Sec6.4: "reads the player's own world position... into a
    // live map-centre pair"). ---------------------------------------------
    {
        MinimapLiveInput in;
        in.staticMinimap = false;
        const double xs[] = {0.0, 125.0, -40.0, 999.5};
        const double zs[] = {0.0, -60.0, 300.0, -12.25};
        const double headings[] = {0.0, 0.5, 3.14159265, -1.2};
        for (int i = 0; i < 4; ++i) {
            in.playerWorldX = xs[i];
            in.playerWorldZ = zs[i];
            in.playerHeadingRad = headings[i];
            const MinimapLiveTransform out = updateMinimapLiveTransform(in);
            CHECK(near(out.mapCentreX, xs[i]) && near(out.mapCentreZ, zs[i]));
            CHECK(near(out.backgroundRotationRad, headings[i])); // static off: live heading drives rotation
        }
    }

    // --- 2. seated-in-vehicle fallback (Sec6.4: "falling back to a linked
    // vehicle's position if the player is seated") - the vehicle's own
    // position/heading is used, NOT the player's stale on-foot position. --
    {
        MinimapLiveInput in;
        in.playerWorldX = 10.0;
        in.playerWorldZ = 20.0;
        in.playerHeadingRad = 1.0; // stale - should be ignored while seated
        in.seatedInVehicle = true;
        in.vehicleWorldX = 500.0;
        in.vehicleWorldZ = -250.0;
        in.vehicleHeadingRad = 2.5;
        in.staticMinimap = false;
        const MinimapLiveTransform out = updateMinimapLiveTransform(in);
        CHECK(near(out.mapCentreX, 500.0) && near(out.mapCentreZ, -250.0));
        CHECK(near(out.backgroundRotationRad, 2.5));
        // Exiting the vehicle (seatedInVehicle=false) reverts to the
        // player's own position/heading immediately - no residual state.
        in.seatedInVehicle = false;
        const MinimapLiveTransform out2 = updateMinimapLiveTransform(in);
        CHECK(near(out2.mapCentreX, 10.0) && near(out2.mapCentreZ, 20.0) && near(out2.backgroundRotationRad, 1.0));
    }

    // --- 3. static-minimap gating (Sec6.4: "reads the static-minimap flag
    // THREE times to gate a live-heading read... vs a fixed/zeroed
    // orientation, and whether a computed rotation is applied... at all" -
    // both gated effects collapse to the same net result here: zero
    // rotation, map-centre panning still live either way). -----------------
    {
        MinimapLiveInput in;
        in.playerWorldX = 50.0;
        in.playerWorldZ = -50.0;
        in.playerHeadingRad = 1.0;
        in.staticMinimap = true;
        MinimapLiveTransform out = updateMinimapLiveTransform(in);
        CHECK(near(out.backgroundRotationRad, 0.0)); // rotation gated OFF
        CHECK(near(out.mapCentreX, 50.0) && near(out.mapCentreZ, -50.0)); // panning still live
        in.playerHeadingRad = 2.75; // heading keeps changing in the background...
        out = updateMinimapLiveTransform(in);
        CHECK(near(out.backgroundRotationRad, 0.0)); // ...but rotation never follows while static
        in.staticMinimap = false; // turning the option back off resumes live rotation at once
        out = updateMinimapLiveTransform(in);
        CHECK(near(out.backgroundRotationRad, 2.75));
        // Same gating applies to the seated/vehicle branch.
        in.seatedInVehicle = true;
        in.vehicleHeadingRad = 0.33;
        in.staticMinimap = true;
        out = updateMinimapLiveTransform(in);
        CHECK(near(out.backgroundRotationRad, 0.0));
    }

    // --- 4. the player marker's own screen position is CONSTANT across
    // different world positions/headings (Sec6.4's single most important
    // confirmed fact: "because the on-screen transform centres the view on
    // the player by construction, the player's own marker sits at a fixed
    // screen point regardless of world position" - NOT a pooled/addable
    // blip, Sec6.1). Proven by feeding the world point EQUAL to the live
    // map-centre (i.e. the player's own position) through the SAME
    // world->screen formula the background/landmarks use: it always lands
    // on the fixed centre, for every position/heading/static combination
    // tried. ------------------------------------------------------------
    {
        const double screenCx = 150.0, screenCy = 80.0, pxPerUnit = 0.2;
        struct Case { double x, z, heading; bool seated, statics; };
        const Case cases[] = {
            {0, 0, 0, false, false},       {300, -120, 0.7, false, false},
            {-45, 90, 2.1, false, true},    {1000, 1000, -0.9, false, false},
            {10, 10, 5.0, true, false},     {10, 10, 5.0, true, true},
        };
        for (const Case& c : cases) {
            MinimapLiveInput in;
            in.playerWorldX = c.x; in.playerWorldZ = c.z; in.playerHeadingRad = c.heading;
            in.seatedInVehicle = c.seated;
            in.vehicleWorldX = c.x; in.vehicleWorldZ = c.z; in.vehicleHeadingRad = c.heading;
            in.staticMinimap = c.statics;
            const MinimapLiveTransform out = updateMinimapLiveTransform(in);
            // The player's own world point is exactly the live map-centre
            // (on foot) or the vehicle's position (seated) - either way,
            // `out.mapCentre*` IS that point, so feeding it back through
            // worldToMinimapScreen must always resolve to the untranslated,
            // unrotated centre: (rx,rz) = (0,0) regardless of rotation.
            // c.x/c.z IS the player's own displayed world point either way
            // (on foot: playerWorld*; seated: vehicleWorld* is set to the
            // SAME c.x/c.z above), so feeding it straight through below is
            // exactly "where is the player's own position drawn."
            const ScreenPoint sp = worldToMinimapScreen(c.x, c.z, out, screenCx, screenCy, pxPerUnit);
            CHECK(near(sp.x, screenCx, 1e-6) && near(sp.y, screenCy, 1e-6));
        }
        // A landmark NOT at the player's own position DOES move between
        // cases (the negative control - proves the formula above is not
        // trivially constant for every input).
        MinimapLiveInput a, b;
        a.playerWorldX = 0; a.playerWorldZ = 0; a.playerHeadingRad = 0; a.staticMinimap = false;
        b.playerWorldX = 200; b.playerWorldZ = 0; b.playerHeadingRad = 0; b.staticMinimap = false;
        const ScreenPoint landmarkA = worldToMinimapScreen(500, 0, updateMinimapLiveTransform(a), screenCx, screenCy, pxPerUnit);
        const ScreenPoint landmarkB = worldToMinimapScreen(500, 0, updateMinimapLiveTransform(b), screenCx, screenCy, pxPerUnit);
        CHECK(!near(landmarkA.x, landmarkB.x, 1e-6)); // the world-fixed landmark panned on screen
        // ...and a heading change rotates a landmark around the fixed centre
        // when static is off.
        MinimapLiveInput c, d;
        c.playerWorldX = 0; c.playerWorldZ = 0; c.playerHeadingRad = 0; c.staticMinimap = false;
        d.playerWorldX = 0; d.playerWorldZ = 0; d.playerHeadingRad = 1.57079632679; d.staticMinimap = false;
        const ScreenPoint landmarkC = worldToMinimapScreen(100, 0, updateMinimapLiveTransform(c), screenCx, screenCy, pxPerUnit);
        const ScreenPoint landmarkD = worldToMinimapScreen(100, 0, updateMinimapLiveTransform(d), screenCx, screenCy, pxPerUnit);
        CHECK(!near(landmarkC.y, landmarkD.y, 1e-6)); // rotation visibly moved the landmark
        // ...but NOT when static is on (the gating negative control).
        MinimapLiveInput e, f;
        e.playerWorldX = 0; e.playerWorldZ = 0; e.playerHeadingRad = 0; e.staticMinimap = true;
        f.playerWorldX = 0; f.playerWorldZ = 0; f.playerHeadingRad = 1.57079632679; f.staticMinimap = true;
        const ScreenPoint landmarkE = worldToMinimapScreen(100, 0, updateMinimapLiveTransform(e), screenCx, screenCy, pxPerUnit);
        const ScreenPoint landmarkF = worldToMinimapScreen(100, 0, updateMinimapLiveTransform(f), screenCx, screenCy, pxPerUnit);
        CHECK(near(landmarkE.x, landmarkF.x, 1e-6) && near(landmarkE.y, landmarkF.y, 1e-6));
        // The CHOSEN fixed-offset constants themselves are plain zero, by
        // construction (Sec6.4's own fixed-centre fact, this project's own
        // CHOSEN draw point - see the header's own top comment).
        CHECK(kPlayerMarkerScreenOffsetX == 0.0 && kPlayerMarkerScreenOffsetY == 0.0);
    }

    // --- 5. sr2_map's own 2 real properties round-trip through the
    // EXISTING typed VINT property store (spec-hud-runtime.md Sec6: zoom
    // float `this+0xa0`, map_mode enum `this+0xa4`) - same store every
    // other element type's properties use, not a parallel mechanism. ------
    {
        EngineState es;
        uint32_t h = es.registerTypedVdoObjectForTesting("corner_minimap", "sr2_map", 0, 0);
        sr3luahost::VintTaggedValue z;
        z.kind = sr3luahost::VintTaggedValue::Kind::Number;
        z.number = 3.0;
        es.setVintProperty(h, "zoom", {z});
        auto got = es.getVintProperty(h, "zoom");
        CHECK(got && got->size() == 1 && (*got)[0].number == static_cast<double>(3.0f));
        CHECK(es.findVintStoredProperty(h, "zoom")->typedAs == kVintCodeFloat);
        sr3luahost::VintTaggedValue mm;
        mm.kind = sr3luahost::VintTaggedValue::Kind::String;
        mm.text = "minimap";
        es.setVintProperty(h, "map_mode", {mm});
        // The SETTER resolves the enum name to its value (CONFIRMED,
        // Sec9.2a/Sec20.1) - findVintStoredProperty reads that resolved
        // value back directly. The GETTER's own push form for an enum-coded
        // property is explicitly OPEN (Sec20.2: "the value pushed... is not
        // specified") - the SAME refusal `element.auto_offset` already hits
        // in tests/synthetic_vint_property_test.cpp (code 0xd=13), so
        // map_mode's getVintProperty() is exercised through throwsOpen()
        // here, not called directly.
        CHECK(es.findVintStoredProperty(h, "map_mode")->values[0].number == static_cast<double>(MinimapMode::Minimap));
        CHECK(throwsOpen([&] { es.getVintProperty(h, "map_mode"); }, "type code 13"));
    }

    if (g_failures == 0) std::cout << "ALL minimap live-transform TESTS PASSED\n";
    return g_failures == 0 ? 0 : 1;
}
