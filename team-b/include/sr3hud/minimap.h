// Corner minimap - spec-hud-runtime.md H.3 (world->screen transform, speed
// zoom, rotation, base texture, blip placement, player glyph) with the H.4(a)
// test vectors. Pure math: no renderer, window or engine-state dependency.
//
// Screen coordinates are the HUD document's 1280x720 reference pixels, y
// down. World coordinates are (X, Z) metres; the game's north is -Z and its
// map east is -X.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace sr3hud {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

// ---------------------------------------------------------------------------
// Layout constants (H.2 minimap row, H.3 points 1, 7, 8)
// ---------------------------------------------------------------------------

// C: base_mini_map (1,0) + map_grp (190,566); game_map has no anchor.
constexpr Vec2 kMinimapCentreRef{191.0, 566.0};
constexpr double kMinimapRingDiameterRef = 148.0;  // map_ring_NE
constexpr double kMinimapBlipRadiusRef = 67.0;     // R67, x UI scale
constexpr double kMinimapBaseMapAlpha = 0.75;      // base map drawn at alpha * 0.75
constexpr double kMinimapClipMarginRef = 64.0;     // clip rect = mask size + 64

// Shipped tweak_table.xtbl values the minimap reads.
struct MinimapTweaks {
    double maxScale = 0.75;            // Minimap_max_scale
    double interiorMagFactor = 1.75;   // Minimap_interior_mag_factor
    double elevationTolerance = 3.5;   // Minimap_elevation_tolerance (consumer not traced)
};

constexpr double kMinimapMphPerMetrePerSecond = 2.23694;

// ---------------------------------------------------------------------------
// Speed zoom (H.3 point 3)
// ---------------------------------------------------------------------------

// The smoothed speed level s (integer mph, 0-100). Each frame:
//   acc += 13*dt; speed = round(|v| * 2.23694) (0 with no vehicle);
//   speed > T  -> T = speed;   T > speed + 5 -> T = speed;
//   s steps toward T by floor(acc), the remainder staying in acc, but never
//   sits below T (an upward change snaps at once); s is capped at 100.
// floor(acc) is taken out of acc every frame, including frames where s
// already equals T (the spec's "the remainder stays in acc").
class MinimapSpeedZoom {
public:
    void update(double vehicleSpeedMetresPerSecond, double dtSeconds);
    int level() const { return s_; }
    int heldTarget() const { return t_; }
    double accumulator() const { return acc_; }

private:
    int s_ = 0;
    int t_ = 0;
    double acc_ = 0.0;
};

// The speed the zoom reads: the current vehicle's, otherwise the last
// vehicle's, otherwise 0 (no vehicle at all).
double MinimapZoomSourceSpeed(std::optional<double> currentVehicleSpeed,
                              std::optional<double> lastVehicleSpeed);

// Z = max(m*m, m - s*(m - m*m)/100), m = Minimap_max_scale.
double MinimapZoom(int speedLevel, const MinimapTweaks& tweaks = {});
// k = Z x (interior factor when interior) x UI scale (document y scale).
double MinimapScale(double zoom, bool interior, double uiScale,
                    const MinimapTweaks& tweaks = {});

// ---------------------------------------------------------------------------
// Rotation (H.3 point 4) and the map centre (point 2)
// ---------------------------------------------------------------------------

// h = atan2(fwd.x, fwd.z) of a forward vector (the camera's in rotating
// mode).
double HeadingFromForward(double fwdX, double fwdZ);

struct MinimapRotation {
    double theta = 0.0;        // theta = -(h + pi)
    double mapTheta = 0.0;     // theta_m: theta in rotating mode, 0 in static mode
    double glyphRotation = 0.0;  // player glyph: 0 rotating; -theta = h + pi static
};

// Rotating mode (static-minimap option off): h is the camera heading.
// Static mode: h is the player's own orientation, and theta is used only
// for the player glyph.
MinimapRotation ComputeMinimapRotation(bool staticMinimap, double cameraHeading,
                                       double playerHeading);

struct MinimapCentreInput {
    double playerX = 0.0, playerZ = 0.0;
    bool inVehicle = false;
    double vehicleX = 0.0, vehicleZ = 0.0;
    // The special camera mode (its global in {1, 3}; identity HYPOTHESIS)
    // centres on a separate camera vector.
    bool specialCameraMode = false;
    double cameraX = 0.0, cameraZ = 0.0;
};
// P: the special camera vector, else the vehicle, else the player.
Vec2 MinimapCentre(const MinimapCentreInput& in);

// ---------------------------------------------------------------------------
// World -> screen (H.3 point 5) and blips (point 8)
// ---------------------------------------------------------------------------

// screen(X,Z) = C + R(theta_m) * k * (-dX, +dZ), dX = X - P.x, dZ = Z - P.z.
Vec2 MinimapWorldToScreen(Vec2 centre, Vec2 mapCentreWorld, double worldX, double worldZ,
                          double k, double mapTheta);

struct BlipPlacement {
    Vec2 screen;
    bool clamped = false;   // r > R67
    double edge = 0.0;      // clamp((r - R67) / (2 R67), 0, 1)
};

// Polar placement: r = k*sqrt(dX^2+dZ^2), a = atan2(dX, dZ) - 3pi/2 + theta_m,
// drawn at min(r, R67), R67 = 67 x UI scale.
BlipPlacement PlaceMinimapBlip(Vec2 centre, Vec2 mapCentreWorld, double worldX, double worldZ,
                               double k, double mapTheta, double uiScale);

// The north marker rides the ring: polar angle pi (toward -Z) at r = R67.
Vec2 MinimapNorthMarker(Vec2 centre, double mapTheta, double uiScale);

// Blip category passes drawn by the draw callback: categories 10, 8, 7, 0,
// 2, 3 gated by bits 0x4, 0x2, 0x1, 0x8, 0x40, 0x80 of the filter mask;
// category 9 always.
bool MinimapCategoryPassEnabled(int category, uint32_t filterMask);

// ---------------------------------------------------------------------------
// Base texture (H.3 point 6 and the interior resolution)
// ---------------------------------------------------------------------------

// The city tile lattice (spec-world-streaming.md 10.6(g)): a 320 x 280 m
// tile, centre_x = 320*col - 4000 - 160*(row mod 2), centre_z = 3640 -
// 280*row, on a 25 x 25 grid.
constexpr int kCityGridSize = 25;
constexpr double kCityTileWidth = 320.0;
constexpr double kCityTileHeight = 280.0;

struct TileCoord {
    int col = 0;
    int row = 0;
};

struct WorldBox {
    double minX = 0.0, minZ = 0.0, maxX = 0.0, maxZ = 0.0;
    double width() const { return maxX - minX; }
    double height() const { return maxZ - minZ; }
    double centreX() const { return 0.5 * (minX + maxX); }
    double centreZ() const { return 0.5 * (minZ + maxZ); }
};

// The tile whose 320 x 280 box contains (x, z). Lower box edges belong to
// the tile (half-open boxes).
TileCoord CityTileAt(double x, double z);
bool InCityGrid(TileCoord t);
WorldBox CityTileBox(TileCoord t);
// "CCRR", e.g. col 18 row 15 -> "1815".
std::string CityTileName(TileCoord t);

// Minimap bounds: the zone box expanded by +-160 m in x and +-116 m in z.
constexpr double kMinimapPadX = 160.0;
constexpr double kMinimapPadZ = 116.0;
WorldBox MinimapPaddedBounds(const WorldBox& zoneBox);

constexpr const char* kMinimapWaterTexture = "sr2_water_minimap.tga";
constexpr const char* kMinimapWorldTexture = "mapsaintsrow.tga";
constexpr const char* kMinimapWorldTexturePeg = "misc-static.tga";

struct MinimapBaseTexture {
    std::string bitmap;  // "minimap_<zone>.tga", the water map, or the world map
    WorldBox bounds;     // the texture's world bounds
    bool interior = false;
};

// Valid zone: "minimap_<zoneName>.tga" with padded zone bounds, or the water
// map (same bounds) when that bitmap is not present. No valid zone: the
// world map with the world box as bounds.
MinimapBaseTexture SelectMinimapBaseTexture(bool zoneValid, const std::string& zoneName,
                                            const WorldBox& zoneBox, bool zoneBitmapPresent,
                                            bool zoneIsInterior, const WorldBox& worldBox);

// u = (xmax - X)/(xmax - xmin), v = (Z - zmin)/(zmax - zmin).
Vec2 MinimapTextureUV(const WorldBox& bounds, double worldX, double worldZ);

struct MinimapQuad {
    Vec2 centre;        // screen centre of the textured quad
    double width = 0.0;   // k * (xmax - xmin)
    double height = 0.0;  // k * (zmax - zmin)
    double rotation = 0.0;  // theta_m; full-texture UVs, u right, v down
};

// Exterior: centre = C + R(theta_m) * k * (P.x - xc, zc - P.z), (xc, zc) the
// bounds centre. Interior: the shipped code uses a reference point that is
// never written, so (xc, zc) is the world origin; correctedInterior = true
// uses the bounds centre instead (the spec's "corrected port" option).
MinimapQuad MinimapBaseQuad(Vec2 centre, Vec2 mapCentreWorld, const MinimapBaseTexture& tex,
                            double k, double mapTheta, bool correctedInterior = false);

}  // namespace sr3hud
