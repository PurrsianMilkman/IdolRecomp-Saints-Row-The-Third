// Corner minimap - spec-hud-runtime.md H.3. See include/sr3hud/minimap.h.

#include "sr3hud/minimap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sr3hud {

namespace {
constexpr double kPi = 3.14159265358979323846;

Vec2 Rotate(Vec2 v, double theta) {
    const double c = std::cos(theta), s = std::sin(theta);
    return {c * v.x - s * v.y, s * v.x + c * v.y};
}
}  // namespace

void MinimapSpeedZoom::update(double vehicleSpeedMetresPerSecond, double dtSeconds) {
    acc_ += 13.0 * dtSeconds;
    const int speed = static_cast<int>(
        std::lround(std::fabs(vehicleSpeedMetresPerSecond) * kMinimapMphPerMetrePerSecond));
    if (speed > t_)
        t_ = speed;
    else if (t_ > speed + 5)
        t_ = speed;
    const double whole = std::floor(acc_);
    acc_ -= whole;
    const int step = static_cast<int>(whole);
    if (s_ < t_)
        s_ = t_;
    else if (s_ > t_)
        s_ = std::max(t_, s_ - step);
    s_ = std::min(s_, 100);
}

double MinimapZoomSourceSpeed(std::optional<double> currentVehicleSpeed,
                              std::optional<double> lastVehicleSpeed) {
    if (currentVehicleSpeed) return *currentVehicleSpeed;
    if (lastVehicleSpeed) return *lastVehicleSpeed;
    return 0.0;
}

double MinimapZoom(int speedLevel, const MinimapTweaks& tweaks) {
    const double m = tweaks.maxScale;
    const double lo = m * m;
    return std::max(lo, m - speedLevel * (m - lo) / 100.0);
}

double MinimapScale(double zoom, bool interior, double uiScale, const MinimapTweaks& tweaks) {
    return zoom * (interior ? tweaks.interiorMagFactor : 1.0) * uiScale;
}

double HeadingFromForward(double fwdX, double fwdZ) {
    return std::atan2(fwdX, fwdZ);
}

MinimapRotation ComputeMinimapRotation(bool staticMinimap, double cameraHeading,
                                       double playerHeading) {
    MinimapRotation r;
    const double h = staticMinimap ? playerHeading : cameraHeading;
    r.theta = -(h + kPi);
    r.mapTheta = staticMinimap ? 0.0 : r.theta;
    r.glyphRotation = staticMinimap ? -r.theta : 0.0;
    return r;
}

Vec2 MinimapCentre(const MinimapCentreInput& in) {
    if (in.specialCameraMode) return {in.cameraX, in.cameraZ};
    if (in.inVehicle) return {in.vehicleX, in.vehicleZ};
    return {in.playerX, in.playerZ};
}

Vec2 MinimapWorldToScreen(Vec2 centre, Vec2 mapCentreWorld, double worldX, double worldZ,
                          double k, double mapTheta) {
    const double dX = worldX - mapCentreWorld.x;
    const double dZ = worldZ - mapCentreWorld.y;
    const Vec2 r = Rotate({-dX * k, dZ * k}, mapTheta);
    return {centre.x + r.x, centre.y + r.y};
}

BlipPlacement PlaceMinimapBlip(Vec2 centre, Vec2 mapCentreWorld, double worldX, double worldZ,
                               double k, double mapTheta, double uiScale) {
    const double dX = worldX - mapCentreWorld.x;
    const double dZ = worldZ - mapCentreWorld.y;
    const double r = k * std::sqrt(dX * dX + dZ * dZ);
    const double a = std::atan2(dX, dZ) - 1.5 * kPi + mapTheta;
    const double r67 = kMinimapBlipRadiusRef * uiScale;
    BlipPlacement p;
    p.clamped = r > r67;
    p.edge = std::clamp((r - r67) / (2.0 * r67), 0.0, 1.0);
    const double drawn = std::min(r, r67);
    p.screen = {centre.x + drawn * std::cos(a), centre.y + drawn * std::sin(a)};
    return p;
}

Vec2 MinimapNorthMarker(Vec2 centre, double mapTheta, double uiScale) {
    const double a = kPi - 1.5 * kPi + mapTheta;
    const double r67 = kMinimapBlipRadiusRef * uiScale;
    return {centre.x + r67 * std::cos(a), centre.y + r67 * std::sin(a)};
}

bool MinimapCategoryPassEnabled(int category, uint32_t filterMask) {
    switch (category) {
    case 10: return (filterMask & 0x4u) != 0;
    case 8: return (filterMask & 0x2u) != 0;
    case 7: return (filterMask & 0x1u) != 0;
    case 0: return (filterMask & 0x8u) != 0;
    case 2: return (filterMask & 0x40u) != 0;
    case 3: return (filterMask & 0x80u) != 0;
    case 9: return true;
    default: return false;
    }
}

TileCoord CityTileAt(double x, double z) {
    TileCoord t;
    // z in [cz - 140, cz + 140) with cz = 3640 - 280*row.
    t.row = static_cast<int>(std::ceil((3500.0 - z) / kCityTileHeight));
    const int parity = ((t.row % 2) + 2) % 2;
    // x in [cx - 160, cx + 160) with cx = 320*col - 4000 - 160*parity.
    t.col = static_cast<int>(std::floor((x + 4160.0 + 160.0 * parity) / kCityTileWidth));
    return t;
}

bool InCityGrid(TileCoord t) {
    return t.col >= 0 && t.col < kCityGridSize && t.row >= 0 && t.row < kCityGridSize;
}

WorldBox CityTileBox(TileCoord t) {
    const int parity = ((t.row % 2) + 2) % 2;
    const double cx = 320.0 * t.col - 4000.0 - 160.0 * parity;
    const double cz = 3640.0 - 280.0 * t.row;
    return {cx - 0.5 * kCityTileWidth, cz - 0.5 * kCityTileHeight,
            cx + 0.5 * kCityTileWidth, cz + 0.5 * kCityTileHeight};
}

std::string CityTileName(TileCoord t) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d%02d", t.col, t.row);
    return buf;
}

WorldBox MinimapPaddedBounds(const WorldBox& b) {
    return {b.minX - kMinimapPadX, b.minZ - kMinimapPadZ, b.maxX + kMinimapPadX,
            b.maxZ + kMinimapPadZ};
}

MinimapBaseTexture SelectMinimapBaseTexture(bool zoneValid, const std::string& zoneName,
                                            const WorldBox& zoneBox, bool zoneBitmapPresent,
                                            bool zoneIsInterior, const WorldBox& worldBox) {
    MinimapBaseTexture t;
    if (!zoneValid) {
        t.bitmap = kMinimapWorldTexture;
        t.bounds = worldBox;
        return t;
    }
    t.bitmap = zoneBitmapPresent ? "minimap_" + zoneName + ".tga" : std::string(kMinimapWaterTexture);
    t.bounds = MinimapPaddedBounds(zoneBox);
    t.interior = zoneIsInterior;
    return t;
}

Vec2 MinimapTextureUV(const WorldBox& b, double worldX, double worldZ) {
    return {(b.maxX - worldX) / b.width(), (worldZ - b.minZ) / b.height()};
}

MinimapQuad MinimapBaseQuad(Vec2 centre, Vec2 mapCentreWorld, const MinimapBaseTexture& tex,
                            double k, double mapTheta, bool correctedInterior) {
    MinimapQuad q;
    q.width = k * tex.bounds.width();
    q.height = k * tex.bounds.height();
    q.rotation = mapTheta;
    double xc = tex.bounds.centreX(), zc = tex.bounds.centreZ();
    if (tex.interior && !correctedInterior) xc = zc = 0.0;
    const Vec2 off = Rotate({k * (mapCentreWorld.x - xc), k * (zc - mapCentreWorld.y)}, mapTheta);
    q.centre = {centre.x + off.x, centre.y + off.y};
    return q;
}

}  // namespace sr3hud
