// Playtest sky (Team C, HANDOFF-C.md section 11, "looks like the game" 2/4): a screen-space sky
// gradient drawn right after the clear, coloured from the default_district.xtbl base curves with the
// current time segment's weather cells (West1..West4, East1..East4, West_Zenith) merged over them per
// field (spec-tables-environment.md 19.1 "Live wiring", D-24; ComputeSceneColours below).
//
// CHOSEN (the sky shader's own use of the nine colours is not specified): bands 1..4 sit at
// elevations 0, 12, 30 and 55 degrees, the zenith colour at 90 (shared by both sides); the West
// set faces -X and the East set +X, mixed by the view ray's x component; below the horizon the
// band-1 colour holds. Colours are colour x intensity, scaled down together when any channel
// exceeds 1. The real skybox meshes (skybox.vpp_pc rfg_skybox / skybox_clouds) are not drawn yet.
#include "viewer_platform.h"
#include "viewer_playtest_session.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "sr3render/tod_lighting.h"
#include "sr3render/weather_schedule.h"
#include "sr3sky/sun.h"
#include "sr3sky/tod_curves.h"

namespace sr3viewer {

namespace {

using Cell = sr3tables_environment::WeatherTimeOfDayCell;
using CI = sr3tables_environment::ColorIntensity;

const CI& W1(const Cell& c) { return c.west1; }
const CI& W2(const Cell& c) { return c.west2; }
const CI& W3(const Cell& c) { return c.west3; }
const CI& W4(const Cell& c) { return c.west4; }
const CI& E1(const Cell& c) { return c.east1; }
const CI& E2(const Cell& c) { return c.east2; }
const CI& E3(const Cell& c) { return c.east3; }
const CI& E4(const Cell& c) { return c.east4; }
const CI& WZ(const Cell& c) { return c.westZenith; }

using Rgb = std::array<float, 3>;

// Inverse of a 4x4 row-major matrix (general cofactor form); false if singular.
bool Invert(const float m[16], float out[16]) {
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-12f) return false;
    for (int i = 0; i < 16; ++i) out[i] = inv[i] / det;
    return true;
}

// Row-vector convention (v * M), as buildWorldMatrix / the viewProjection the renderer uploads.
std::array<float, 3> Unproject(const float inv[16], float x, float y, float z) {
    const float v[4] = {x, y, z, 1.0f};
    float r[4] = {0, 0, 0, 0};
    for (int c = 0; c < 4; ++c)
        for (int k = 0; k < 4; ++k) r[c] += v[k] * inv[k * 4 + c];
    return {r[0] / r[3], r[1] / r[3], r[2] / r[3]};
}

Rgb Lerp(const Rgb& a, const Rgb& b, float t) {
    return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
}

// The colour at elevation `deg` along one side's bands.
Rgb Bands(const Rgb side[4], const Rgb& zenith, float deg) {
    static const float kElev[5] = {0.0f, 12.0f, 30.0f, 55.0f, 90.0f};
    const Rgb pts[5] = {side[0], side[1], side[2], side[3], zenith};
    if (deg <= 0.0f) return pts[0];
    for (int i = 0; i < 4; ++i)
        if (deg <= kElev[i + 1]) return Lerp(pts[i], pts[i + 1], (deg - kElev[i]) / (kElev[i + 1] - kElev[i]));
    return zenith;
}

// The scene colours (spec-tables-environment 19.1 "Live wiring" and D-24, FINAL): every value is the
// default_district.xtbl base curve at the hour, with the current time segment's weather cells merged
// over it per field (sr3sky::MergeWeatherField): "from" = the cell of the stage being left, "to" = the
// stage being entered, t = the stage transition fraction. A field a cell does not author keeps the base;
// the neighbouring segment's cells are not read. The merge is off while a script forces the weather;
// the playtest has no script weather override, so it is always on here.
struct SceneColours {
    Rgb sky[9];  // West1..4, East1..4, zenith (base curves: west0..3, east0..3, zenith)
    Rgb ambient, sun;
};

bool ComputeSceneColours(PlaytestSession& sess, double todFraction, SceneColours& out) {
    if (sess.todSegments.empty()) return false;
    if (!sess.todBaseCurvesTried) {
        sess.todBaseCurvesTried = true;
        try {
            sess.todBaseCurves = sr3sky::TodBaseCurves::Parse(sess.docOf("default_district.xtbl"));
            std::printf("lighting : default_district.xtbl base TOD curves: %zu (spec-tables-environment 19.1)\n",
                        sess.todBaseCurves->size());
        } catch (const std::exception& e) {
            std::printf("lighting : default_district.xtbl not loaded (%s) - weather cells only\n", e.what());
        }
    }
    const sr3render::TodSegmentBracket br = sr3render::FindTodSegmentBracket(sess.todSegments, static_cast<float>(todFraction));
    if (!br.low) return false;
    std::string rawA, rawB;
    if (sess.weatherSched) {
        const auto st = sess.weatherSched->Current();
        const auto& stages = sess.weatherSched->Stages();
        if (st.stageA >= 0 && st.stageA < static_cast<int>(stages.size())) rawA = stages[static_cast<size_t>(st.stageA)].name;
        if (st.stageB >= 0 && st.stageB < static_cast<int>(stages.size())) rawB = stages[static_cast<size_t>(st.stageB)].name;
    }
    const Cell* from = rawA.empty() ? nullptr : sr3render::FindCellByStageName(*br.low, rawA);
    const Cell* to = rawB.empty() ? nullptr : sr3render::FindCellByStageName(*br.low, rawB);
    const bool haveBase = sess.todBaseCurves && !sess.todBaseCurves->empty();
    if (!haveBase && !from && !to) return false;  // nothing to light with: the clear colour stays
    const float hour = sr3sky::HourFromDayFraction(todFraction);
    const float t = sess.weatherLightBlend;
    // CHOSEN: a base curve missing from the table reads as black at intensity 1 (the cells may still author it).
    auto field = [&](const char* baseName, sr3render::ColorIntensityExtractor ex) {
        sr3sky::Value4 base{0.0f, 0.0f, 0.0f, 1.0f};
        if (haveBase)
            if (const auto v = sess.todBaseCurves->sample(baseName, hour)) base = *v;
        auto cellValue = [&](const Cell* c, sr3sky::Value4& v) -> const sr3sky::Value4* {
            if (!c || !ex(*c).present) return nullptr;
            const sr3render::TodColor4 c4 = sr3render::ToTodColor4(ex(*c));
            v = {c4.r, c4.g, c4.b, c4.intensity};
            return &v;
        };
        sr3sky::Value4 fv, tv;
        const sr3sky::Value4 m = sr3sky::MergeWeatherField(base, cellValue(from, fv), cellValue(to, tv), t);
        return Rgb{m[0] * m[3], m[1] * m[3], m[2] * m[3]};  // the HDR pack: rgb x intensity
    };
    static const char* kSky[9] = {"west0", "west1", "west2", "west3", "east0", "east1", "east2", "east3", "zenith"};
    const sr3render::ColorIntensityExtractor ex[9] = {W1, W2, W3, W4, E1, E2, E3, E4, WZ};
    for (int i = 0; i < 9; ++i) out.sky[i] = field(kSky[i], ex[i]);
    out.ambient = field("ambient_color", sr3render::ExtractAmbientColor);
    out.sun = field("tod_light_color", sr3render::ExtractTodLightColor);
    return true;
}

}  // namespace

void drawSkyGradient(PlaytestSession& sess, const float viewProjection[16], double todFraction) {
    if (!sess.hudEnabled || !sess.hudPipe_ || sess.todSegments.empty()) return;  // the quad pipeline is set up with the HUD only
    SceneColours sc;
    if (!ComputeSceneColours(sess, todFraction, sc)) return;  // no colours for this stage: the clear colour stays
    Rgb col[9];
    float maxc = 0.0f;
    for (int i = 0; i < 9; ++i) {
        col[i] = sc.sky[i];
        maxc = std::max({maxc, col[i][0], col[i][1], col[i][2]});
    }
    if (maxc <= 0.0f) return;  // the stage carries no skybox colours
    // CHOSEN (labelled per review LG-4): one shared scale-down when any channel exceeds 1.0, keeping the hues
    // and the band-to-band ratios (the real HDR tonemap is OPEN).
    const auto rise = sr3sky::SunTowardDirection(7.5f);
    const float rl = std::sqrt(rise[0] * rise[0] + rise[2] * rise[2]);
    const float kEastX = rl > 0 ? rise[0] / rl : -1.0f, kEastZ = rl > 0 ? rise[2] / rl : 0.0f;
    if (maxc > 1.0f)
        for (Rgb& c : col)
            for (float& v : c) v /= maxc;
    const Rgb west[4] = {col[0], col[1], col[2], col[3]};
    const Rgb east[4] = {col[4], col[5], col[6], col[7]};

    float inv[16];
    if (!Invert(viewProjection, inv)) return;
    VintQuadPipeline& pipe = *sess.hudPipe_;
    ID3D11DeviceContext* ctx = sess.device_->context();
    const uint32_t w = sess.swapChain_->width(), h = sess.swapChain_->height();
    pipe.begin(ctx, w, h, false);
    constexpr int kCols = 12, kRows = 96;  // thin strips: the gradient runs mostly vertically
    for (int ry = 0; ry < kRows; ++ry) {
        for (int rx = 0; rx < kCols; ++rx) {
            const float ndcX = ((rx + 0.5f) / kCols) * 2.0f - 1.0f;
            const float ndcY = 1.0f - ((ry + 0.5f) / kRows) * 2.0f;
            const auto nearP = Unproject(inv, ndcX, ndcY, 0.0f);
            const auto farP = Unproject(inv, ndcX, ndcY, 1.0f);
            float d[3] = {farP[0] - nearP[0], farP[1] - nearP[1], farP[2] - nearP[2]};
            const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len <= 0.0f) continue;
            for (float& v : d) v /= len;
            const float elev = std::asin(std::clamp(d[1], -1.0f, 1.0f)) * 57.29578f;
            const float horiz = std::sqrt(d[0] * d[0] + d[2] * d[2]);
            // East = the sunrise direction of the spec sun path (19.2: toward-sun at 7:30 is (-0.84, 0.54, 0), i.e. -X) - review LG-4.
            const float eastness = horiz > 1e-4f ? 0.5f + 0.5f * (d[0] * kEastX + d[2] * kEastZ) / horiz : 0.5f;
            const Rgb c = Lerp(Bands(west, col[8], elev), Bands(east, col[8], elev), eastness);
            const uint8_t rgba[4] = {static_cast<uint8_t>(std::clamp(c[0], 0.0f, 1.0f) * 255.0f),
                                     static_cast<uint8_t>(std::clamp(c[1], 0.0f, 1.0f) * 255.0f),
                                     static_cast<uint8_t>(std::clamp(c[2], 0.0f, 1.0f) * 255.0f), 255};
            pipe.rect(ctx, static_cast<double>(w) * rx / kCols, static_cast<double>(h) * ry / kRows,
                      static_cast<double>(w) * (rx + 1) / kCols, static_cast<double>(h) * (ry + 1) / kRows, rgba);
        }
    }
    // Back to the device defaults the 3D pass that follows expects (depth test on, opaque, default raster).
    ctx->OMSetDepthStencilState(nullptr, 0);
    ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx->RSSetState(nullptr);
}

// Scene exposure (looks-like-the-game 3/4). sr3render::ComputeTodLightingBlended normalises the ambient and the
// sun colours EACH to a peak of 1.0, so the ambient term alone is full-bright and ambient + sun saturates (the
// washed-out look). CHOSEN stand-in for the unmodelled HDR tonemap (spec-tables-environment 3.3 lists ldr/bloom/
// tonemap floats, not their formula): take the real blended Ambient_Color and TOD_Light_Color (colour x intensity)
// and scale BOTH by one factor so their brightest channels sum to 1.0 - the same balance as the shader's old fixed
// 0.35 + 0.65 - keeping the data's ambient : sun ratio; never scaled up, so dim (night) data stays dim.
void applySceneExposure(PlaytestSession& sess, double todFraction, float ambient[3], float sun[3]) {
    if (!sess.hudEnabled || sess.todSegments.empty()) return;
    SceneColours sc;
    if (!ComputeSceneColours(sess, todFraction, sc)) return;
    const float A[3] = {sc.ambient[0], sc.ambient[1], sc.ambient[2]};
    const float S[3] = {sc.sun[0], sc.sun[1], sc.sun[2]};
    const float peak = std::max({A[0], A[1], A[2]}) + std::max({S[0], S[1], S[2]});
    if (!(peak > 0.0f)) return;
    // CHOSEN lifts for what is not modelled (HDR eye adaptation, indirect/bounce light): the brightest
    // channels sum to 1.25 instead of 1.0 (the shader saturates), and the ambient term counts twice.
    constexpr float kTarget = 1.25f, kAmbientLift = 2.0f;
    const float k = peak > kTarget ? kTarget / peak : 1.0f;
    for (int i = 0; i < 3; ++i) {
        ambient[i] = std::min(1.0f, A[i] * k * kAmbientLift);
        sun[i] = S[i] * k;
    }
    static bool logged = false;
    if (!logged) {
        logged = true;
        std::printf("lighting : exposure k=%.4f (raw ambient %.2f %.2f %.2f, sun %.2f %.2f %.2f) [CHOSEN tonemap stand-in]\n",
                    k, A[0], A[1], A[2], S[0], S[1], S[2]);
    }
}

}  // namespace sr3viewer
