// Real HUD art in the playtest (Team C, HANDOFF-C.md section 11, "looks like the game" 4/4): HUD bitmaps
// are sub-images of shared sheets (spec-hud-runtime.md H.7 (c)): the sheet page textures come from the
// interface-backend peg (misc.vpp_pc, record "<sheet>.tga"), the sub-image rectangles from
// misc_tables.vpp_pc bitmap_sheets.xtbl -> <sheet>.xtbl (StartX, StartY, ImageWidth, ImageHeight).
// Wired: the health bar fill draws `ui_hud_meter_fill` (ui_bms_01 (311,1,290,12)) tinted with the bar
// colour, and the police notoriety icons draw `ui_hud_not_police`; each falls back to a flat rectangle
// when its art is not found. CHOSEN: the bar keeps the playtest's
// own layout (the retail health ring is a bitmap_circle element not reproduced here).
#include "viewer_platform.h"
#include "viewer_playtest_session.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "sr3tables_environment/tables.h"

namespace sr3viewer {

namespace {

struct HudImage {
    ID3D11ShaderResourceView* srv = nullptr;
    uint32_t texW = 0, texH = 0;
    int x = 0, y = 0, w = 0, h = 0;
};

bool FindInArchives(PlaytestSession& sess, const std::string& name, std::vector<uint8_t>& out) {
    for (const auto& a : sess.archives) {
        std::vector<uint8_t> sib;
        if (findEntry(*a->top, name, out, sib, std::string()) && !out.empty()) return true;
    }
    return false;
}

// One named sub-image, loaded once (nullopt if not found).
std::optional<HudImage> LoadHudImage(PlaytestSession& sess, const std::string& imageName) {
    const sr3xtbl::Document bsDoc = sess.docOf("bitmap_sheets.xtbl");
    const auto sheets = sr3tables_environment::ParseBitmapSheetsTable(bsDoc);
    std::printf("hud-art  : bitmap_sheets.xtbl %zu sheet(s)\n", sheets.size());
    for (const auto& s : sheets) {
        const std::string sheet = s.NameOrDefault();
        if (sheet.empty()) continue;
        sr3xtbl::Document sx;
        try {
            sx = sess.docOf(sheet + ".xtbl");
        } catch (const std::exception&) {
            continue;
        }
        for (const sr3xtbl::Node* row : sr3xtbl::Children(sx.table(), "BitmapSheets"))
            for (const sr3xtbl::Node* p : sr3xtbl::Children(sr3xtbl::FindChild(row, "Images"), "Properties")) {
                const std::string* nm = sr3xtbl::ChildText(p, "Name");
                if (!nm || !sr3xtbl::NameEquals(*nm, imageName)) continue;
                auto num = [&](const char* k) {
                    const std::string* t = sr3xtbl::ChildText(p, k);
                    return t ? sr3xtbl::ParseInt32(*t) : 0;
                };
                HudImage img{nullptr, 0, 0, num("StartX"), num("StartY"), num("ImageWidth"), num("ImageHeight")};
                for (const auto& a : sess.archives) {
                    sr3render::UploadedTexture ut;
                    std::string foundIn;
                    if (findAndUploadTexture(*a->top, sheet + ".tga", sess.device_->device(), ut, foundIn, &a->texCache)) {
                        img.srv = ut.srv;
                        img.texW = ut.width;
                        img.texH = ut.height;
                        std::printf("hud-art  : %s -> %s (%d,%d,%dx%d) page %ux%u\n", imageName.c_str(), sheet.c_str(),
                                    img.x, img.y, img.w, img.h, img.texW, img.texH);
                        return img;
                    }
                }
                std::printf("hud-art  : %s found in %s but its page %s.tga is not\n", imageName.c_str(), sheet.c_str(), sheet.c_str());
                return std::nullopt;
            }
    }
    return std::nullopt;
}

// One image per name, looked up once and kept on the session (review LG-8: a static map keyed by the session
// pointer could hand a later session at the same address a texture of the old device; Team B's 987ea9e soak
// crash 2026-10-20, seed 11 round 2: the old session's SRVs are released at teardown). The session owns the
// cache, so it goes with the session. Returns null when the art is not found.
const PlaytestSession::HudArtImage* HudArt(PlaytestSession& sess, const std::string& name) {
    auto it = sess.hudArtImages.find(name);
    if (it == sess.hudArtImages.end()) {
        std::optional<HudImage> img;
        try {
            img = LoadHudImage(sess, name);
        } catch (const std::exception&) {
        }
        PlaytestSession::HudArtImage art;
        if (!img) std::printf("hud-art  : %s not found - flat fallback\n", name.c_str());
        else art = {img->srv, img->texW, img->texH, img->x, img->y, img->w, img->h};
        it = sess.hudArtImages.emplace(name, art).first;
    }
    return (it->second.srv && it->second.texW) ? &it->second : nullptr;
}

// The image's sub-rect stretched over (x0,y0)-(x1,y1), tinted; a flat rect when the art is missing.
void DrawHudArt(PlaytestSession& sess, const std::string& name, double x0, double y0, double x1, double y1,
                const uint8_t rgba[4]) {
    VintQuadPipeline& pipe = *sess.hudPipe_;
    ID3D11DeviceContext* ctx = sess.device_->context();
    const PlaytestSession::HudArtImage* im = HudArt(sess, name);
    if (!im) {
        pipe.rect(ctx, x0, y0, x1, y1, rgba);
        return;
    }
    const double u0 = static_cast<double>(im->x) / im->texW, v0 = static_cast<double>(im->y) / im->texH;
    const double u1 = static_cast<double>(im->x + im->w) / im->texW, v1 = static_cast<double>(im->y + im->h) / im->texH;
    const double pos[4][2] = {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}};
    const double uv[4][2] = {{u0, v0}, {u1, v0}, {u0, v1}, {u1, v1}};
    pipe.quad(ctx, im->srv, pos, uv, rgba, 0, im->texW, im->texH);
}

}  // namespace

void drawHudBarFill(PlaytestSession& sess, double x0, double y0, double x1, double y1, const uint8_t rgba[4]) {
    DrawHudArt(sess, "ui_hud_meter_fill", x0, y0, x1, y1, rgba);
}

void drawHudNotorietyIcons(PlaytestSession& sess, double cx, double cy, double ui, int lit) {
    if (!sess.hudEnabled || !sess.hudPipe_) return;
    // police_noto_icon_1.._5 relative to map_grp (whose origin is the minimap centre, H.3 (191,566)); the
    // icon bitmap is 52x58 at scale 0.5. Icons past `lit` are hidden (the remove animation's end state).
    // CHOSEN: each icon is centred on its offset, drawn white and fully opaque; the add/remove/pulse/decay
    // tweens are not played.
    static const double kOffset[5][2] = {{-70, 60}, {-38, 83}, {0, 89}, {38, 83}, {70, 60}};
    const double hw = 52.0 * 0.5 * 0.5 * ui, hh = 58.0 * 0.5 * 0.5 * ui;
    const uint8_t white[4] = {255, 255, 255, 255};
    for (int i = 0; i < 5 && i < lit; ++i) {
        const double x = cx + kOffset[i][0] * ui, y = cy + kOffset[i][1] * ui;
        DrawHudArt(sess, "ui_hud_not_police", x - hw, y - hh, x + hw, y + hh, white);
    }
}

// A vint bitmap_circle (spec-vint-render.md 13.2): num_wedges independent triangles centre -> rim(a_k) ->
// rim(a_k+1), a swept from start to end in equal steps (radians, clockwise on screen), the rim at the half
// extents along screen axes, the rim UV on the ellipse inscribed in the image's sub-rect, one colour for
// every vertex. Each triangle goes through the quad strip with its last vertex repeated (a degenerate
// second triangle). The wedge count is clamped to 1..256 (the spec's stack-overrun warning).
void drawHudBitmapCircle(PlaytestSession& sess, const std::string& image, double cx, double cy, double halfW,
                         double halfH, double startAngle, double endAngle, int numWedges, const uint8_t rgba[4]) {
    if (!sess.hudEnabled || !sess.hudPipe_) return;
    const PlaytestSession::HudArtImage* im = HudArt(sess, image);
    if (!im) return;  // no flat fallback for a pie
    const int n = (std::max)(1, (std::min)(256, numWedges));
    const double uc = (im->x + im->w * 0.5) / im->texW, vc = (im->y + im->h * 0.5) / im->texH;
    const double ur = im->w * 0.5 / im->texW, vr = im->h * 0.5 / im->texH;
    VintQuadPipeline& pipe = *sess.hudPipe_;
    ID3D11DeviceContext* ctx = sess.device_->context();
    for (int k = 0; k < n; ++k) {
        const double a0 = startAngle + (endAngle - startAngle) * k / n;
        const double a1 = startAngle + (endAngle - startAngle) * (k + 1) / n;
        const double pos[4][2] = {{cx, cy},
                                  {cx + halfW * std::cos(a0), cy + halfH * std::sin(a0)},
                                  {cx + halfW * std::cos(a1), cy + halfH * std::sin(a1)},
                                  {cx + halfW * std::cos(a1), cy + halfH * std::sin(a1)}};
        const double uv[4][2] = {{uc, vc},
                                 {uc + ur * std::cos(a0), vc + vr * std::sin(a0)},
                                 {uc + ur * std::cos(a1), vc + vr * std::sin(a1)},
                                 {uc + ur * std::cos(a1), vc + vr * std::sin(a1)}};
        pipe.quad(ctx, im->srv, pos, uv, rgba, 0, im->texW, im->texH);
    }
}

void drawHudRespectMeter(PlaytestSession& sess, double ui, double pct) {
    // respect_meter (spec-hud-runtime.md H.2 "Respect bar and level"): end_angle = 2.65 - 5.25 x clamp(pct),
    // the element at respect_grp -> (1135, 87) in the 1280x720 reference. CHOSEN (not in the spec): the
    // start angle 2.65 (so pct 0 is empty), the image's own sub-rect size as the element size, white, the
    // default 16 wedges; respect_meter_bg (end - 0.03) and the level/amount texts are not drawn.
    const PlaytestSession::HudArtImage* im = HudArt(sess, "ui_hud_respect_fill");
    if (!im) return;
    const double p = (std::max)(0.0, (std::min)(1.0, pct));
    const uint8_t white[4] = {255, 255, 255, 255};
    drawHudBitmapCircle(sess, "ui_hud_respect_fill", 1135.0 * ui, 87.0 * ui, im->w * 0.5 * ui, im->h * 0.5 * ui, 2.65,
                        2.65 - 5.25 * p, 16, white);
}

}  // namespace sr3viewer
