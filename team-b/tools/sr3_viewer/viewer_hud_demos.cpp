#include "viewer_platform.h"
#include "viewer_modes.h"
#include "viewer_city_support.h"
#include "viewer_common.h"
#include "viewer_hud_support.h"
#include "viewer_vint_quad.h"
#include "sr3render/device.h"
#include "sr3render/mesh_renderer.h"
#include "sr3render/png_writer.h"
#include "sr3render/quad_renderer.h"
#include "sr3render/sun_shadow_map.h"
#include "sr3render/swap_chain.h"
#include "sr3render/tod_clock.h"
#include "sr3render/tod_lighting.h"
#include "sr3render/window.h"
#include "sr3render/texture_upload.h"
#include "sr3luahost/character_damage_sink.h"
#include "sr3luahost/auto_combat.h"
#include "vpp/container.h"
#include "sr3luahost/engine_state.h"
#include "sr3luahost/vint_property_types.h"
#include "sr3luahost/minimap_live_transform.h"
#include "sr3luahost/host.h"
#include "sr3luahost/menu_runtime.h"
#include "sr3luahost/stub_registry.h"
#include "sr3font/vint_font.h"
#include "sr3font/vint_text.h"

using namespace sr3viewer;



int runHudLiveDemo(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: sr3_viewer hud-live-demo <registration_list.txt> <out_prefix>\n"
                     "                   --textures <archive.vpp_pc> [--textures ...] [--fonts <archive.vpp_pc>]...\n"
                     "       Focused HUD overlay (spec-hud-runtime.md): health/ammo/money/notoriety, driven by\n"
                     "       the REAL Lua natives through a real Host/EngineState, rendered through this\n"
                     "       project's own VINT text pipeline (same calls as `vint-doc`'s real text, U5) - see\n"
                     "       runHudLiveDemo()'s own doc comment for the full CONFIRMED/CHOSEN/OPEN breakdown.\n");
        return 1;
    }
    const std::string registrationListPath = argv[2];
    const std::string outPrefix = argv[3];
    std::vector<std::string> textureArchivePaths, fontArchivePaths;
    for (int i = 4; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--textures" && i + 1 < argc) textureArchivePaths.push_back(argv[++i]);
        else if (a == "--fonts" && i + 1 < argc) fontArchivePaths.push_back(argv[++i]);
        else { std::fprintf(stderr, "unrecognised argument '%s'\n", a.c_str()); return 2; }
    }
    if (fontArchivePaths.empty()) fontArchivePaths = textureArchivePaths;
    const uint32_t width = 1280, height = 720;

    std::printf("=== hud-live-demo: live core-stat HUD overlay (spec-hud-runtime.md) ===\n");

    sr3render::RenderDevice device;
    std::string error;
    if (!device.initialise(width, height, error)) { std::fprintf(stderr, "device init failed: %s\n", error.c_str()); return 1; }
    std::printf("device     : %s (%s), target %ux%u RGBA8 offscreen\n",
                device.kind() == sr3render::DeviceKind::Hardware ? "HARDWARE" : "WARP (software)",
                device.adapterName().empty() ? "unnamed adapter" : device.adapterName().c_str(), device.width(),
                device.height());

    std::vector<std::unique_ptr<CityArchive>> fontArchivesOwned;
    std::vector<CityArchive*> fontArchives;
    for (const auto& p : fontArchivePaths) {
        auto a = std::make_unique<CityArchive>();
        a->path = p;
        const size_t slash = p.find_last_of("/\\");
        a->label = slash == std::string::npos ? p : p.substr(slash + 1);
        a->bytes = mapFileLargeC(p);
        if (a->bytes.empty()) { std::fprintf(stderr, "could not read %s\n", p.c_str()); return 1; }
        a->top = std::make_unique<vpp::Container>(vpp::ByteView(a->bytes.data(), a->bytes.size()));
        fontArchives.push_back(a.get());
        fontArchivesOwned.push_back(std::move(a));
    }

    // One hardcoded font (game id 0, "font_body") - see HudDemoFont's own
    // top comment for why this command never resolves a per-element `font`
    // property name the way runVintDoc()'s drawText() does.
    HudDemoFont bodyFont;
    {
        const std::string file = std::string(sr3font::gameFontFileStem(0)) + ".vf3_pc";
        std::string parseError;
        for (CityArchive* a : fontArchives) {
            std::vector<uint8_t> bytes, sib;
            if (!findEntry(*a->top, file, bytes, sib, std::string()) || bytes.empty()) continue;
            try {
                bodyFont.font = sr3font::parseFont(bytes);
                bodyFont.loaded = true;
            } catch (const sr3font::FontFormatError& ex) { parseError = ex.what(); }
            break;
        }
        if (!bodyFont.loaded) {
            std::fprintf(stderr, "font %s not found/parsed in any --fonts/--textures archive (%s)\n", file.c_str(),
                         parseError.c_str());
            return 1;
        }
        for (CityArchive* a : fontArchives) {
            sr3render::UploadedTexture ut;
            std::string foundIn;
            if (findAndUploadTexture(*a->top, bodyFont.font.textureName, device.device(), ut, foundIn, &a->texCache)) {
                bodyFont.tex.srv = ut.srv;
                bodyFont.tex.texW = ut.width;
                bodyFont.tex.texH = ut.height;
                break;
            }
        }
        if (!bodyFont.tex.srv) {
            std::fprintf(stderr, "font glyph texture %s not found in any archive\n", bodyFont.font.textureName.c_str());
            return 1;
        }
        std::printf("font       : %s loaded, %u glyphs, glyph texture %s found\n", file.c_str(), bodyFont.font.glyphCount,
                    bodyFont.font.textureName.c_str());
    }

    VintQuadPipeline pipe;
    if (!pipe.initialise(device.device(), width, height, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    device.bindRenderTargetWithDepth(pipe.depthView());
    pipe.begin(device.context(), width, height, false);

    // ---- the real Lua host (same scaffold tools/lua_host_run.cpp drives) ----
    std::vector<sr3luahost::RegisteredName> allNames = sr3luahost::loadTaggedRegistrationList(registrationListPath);
    // Printed as a basename only (not the full path) so this line stays
    // byte-identical regardless of which absolute checkout path this
    // command runs from - same convention the texture/tables archive labels
    // elsewhere in this file already follow.
    {
        const size_t slash = registrationListPath.find_last_of("/\\");
        const std::string regListLabel =
            slash == std::string::npos ? registrationListPath : registrationListPath.substr(slash + 1);
        std::printf("lua host   : %zu registered names loaded from %s\n", allNames.size(), regListLabel.c_str());
    }
    sr3luahost::Host host(allNames);
    sr3luahost::EngineState& es = host.engineState();
    lua_State* gp = host.gameplayState();
    lua_State* ui = host.uiState();
    auto runOrReport = [&](lua_State* L, const std::string& src) {
        sr3luahost::Host::RunResult r = host.runChunk(L, src, "hud_live_demo.lua");
        if (!r.loadOk) std::printf("  [lua] LOAD ERROR for '%s': %s\n", src.c_str(), r.loadError.c_str());
        else if (!r.pcallOk) std::printf("  [lua] PCALL ERROR for '%s': %s\n", src.c_str(), r.pcallError.c_str());
        return r;
    };

    // ---- CHOSEN test setup (documented, not a spec fact): a single resolved
    // character "player", a single resolved weapon "pistol", and a single-
    // player session that IS its own host - the last one needed only so
    // notoriety_set's own CONFIRMED host-gate (spec-lua-api-behaviour.md
    // Sec6.7/Sec6.22, coopLocalIsHost()) takes its local-apply branch rather
    // than its replicate-only one; spec-lua-api-behaviour.md Sec26.28 itself
    // flags "whether single player installs a one-member host session" as
    // OPEN, so this is this command's own CHOSEN stand-in, not a traced fact.
    es.objectResolves().set("player", true);
    es.weaponDefinitionResolves().set("pistol", true);
    es.hasLocalPlayer().set(true);
    es.coopSession().present.set(true);
    es.coopSession().localIsHost.set(true);
    // CHOSEN, same precedent as present/localIsHost just above: a single-
    // player session has exactly 1 member (itself), well under coop_is_
    // active()'s own CONFIRMED ">= 2" gate (Sec3.1) - so any script that
    // calls coop_is_active() gets a real "false" answer instead of hitting
    // memberCount's OPEN-state throw (that OPEN state is for a REAL multi-
    // member co-op session, which this CHOSEN single-player setup never
    // claims to model). otherMembersPassSlotCheck stays OPEN: condition 3
    // (memberCount < 2) always short-circuits before condition 4 is read.
    es.coopSession().memberCount.set(1);
    es.registerNotorietyAliasTrackCodeForTesting("Police", 3); // CHOSEN track-name binding - see notoriety block below

    // ---- the overlay's own VdoObjects (CHOSEN document, REAL property store) ----
    auto setOffset = [&](uint32_t h, double x, double y) {
        sr3luahost::VintTaggedValue vx, vy;
        vx.kind = sr3luahost::VintTaggedValue::Kind::Number; vx.number = x;
        vy.kind = sr3luahost::VintTaggedValue::Kind::Number; vy.number = y;
        es.setVintProperty(h, "offset", {vx, vy});
    };
    auto setLabelText = [&](uint32_t h, const std::string& text) {
        sr3luahost::VintTaggedValue v;
        v.kind = sr3luahost::VintTaggedValue::Kind::String; v.text = text;
        es.setVintProperty(h, "text_tag", {v});
    };
    const uint32_t hHealth = es.registerTypedVdoObjectForTesting("hud_health_text", "text", 0, 0);
    const uint32_t hAmmo = es.registerTypedVdoObjectForTesting("hud_ammo_text", "text", 0, 0);
    const uint32_t hMoney = es.registerTypedVdoObjectForTesting("hud_money_text", "text", 0, 0);
    const uint32_t hNotoriety = es.registerTypedVdoObjectForTesting("hud_notoriety_text", "text", 0, 0);
    setOffset(hHealth, 40, 24);
    setOffset(hAmmo, 40, 108);
    setOffset(hMoney, 40, 148);
    setOffset(hNotoriety, 40, 188);
    const uint8_t white[4] = {255, 255, 255, 255};
    const double barX0 = 40, barX1 = 340, barY0 = 56, barY1 = 84;
    const uint8_t barBg[4] = {60, 20, 20, 255};

    auto renderFrame = [&](const std::string& path, double hpRatio) {
        device.clear({18, 18, 22, 255});
        pipe.rect(device.context(), barX0, barY0, barX1, barY1, barBg);
        if (hpRatio > 0.0) {
            const double clamped = hpRatio < 1.0 ? hpRatio : 1.0;
            const uint8_t fg[4] = {static_cast<uint8_t>(220 - 140 * clamped), static_cast<uint8_t>(60 + 140 * clamped), 40,
                                   255};
            pipe.rect(device.context(), barX0, barY0, barX0 + (barX1 - barX0) * clamped, barY1, fg);
        }
        drawHudLabel(es, hHealth, bodyFont, pipe, device.context(), white);
        drawHudLabel(es, hAmmo, bodyFont, pipe, device.context(), white);
        drawHudLabel(es, hMoney, bodyFont, pipe, device.context(), white);
        drawHudLabel(es, hNotoriety, bodyFont, pipe, device.context(), white);
        std::vector<uint8_t> pixels;
        if (!device.readBack(pixels, error)) { std::fprintf(stderr, "readback failed: %s\n", error.c_str()); return false; }
        if (!sr3render::writePng(path, width, height, pixels, error)) {
            std::fprintf(stderr, "png write failed: %s\n", error.c_str());
            return false;
        }
        std::printf("wrote      : %s\n", path.c_str());
        return true;
    };

    // ========================= Frame 0: baseline =========================
    // Health: get_max_hit_points (CONFIRMED native, HIGH-CONFIDENCE cadence
    // - polled here, CHOSEN) draws this project's own random-once spawn max
    // (EngineState::drawOrGetMaxHitPoints, spec-lua-api-behaviour.md
    // Sec34.2); set_current_hit_points (CONFIRMED native) then applies the
    // CONFIRMED spawn rule "starts equal to the newly-computed max" (Sec34.1
    // step 2) so frame 0 shows full health.
    runOrReport(gp, "g_maxhp = get_max_hit_points('player')");
    const double maxHp = readLuaGlobalNumber(gp, "g_maxhp");
    runOrReport(gp, "set_current_hit_points('player', g_maxhp)");
    runOrReport(gp, "g_hp = get_current_hit_points('player')");
    double hp = readLuaGlobalNumber(gp, "g_hp");
    std::printf("health     : max=%.0f (drawn by get_max_hit_points, Sec34.2) current=%.0f (full-health spawn rule, "
                "Sec34.1 step 2)\n",
                maxHp, hp);
    setLabelText(hHealth, "HEALTH " + std::to_string(static_cast<int>(hp)) + "/" + std::to_string(static_cast<int>(maxHp)));
    setLabelText(hAmmo, "AMMO --");
    setLabelText(hMoney, "MONEY $" + std::to_string(static_cast<int>(es.playerCash() / 100.0)));
    setLabelText(hNotoriety, "NOTORIETY LEVEL 0");
    renderFrame(outPrefix + "_frame0_baseline.png", maxHp > 0 ? hp / maxHp : 0.0);

    // ========================= Frame 1: damage ===========================
    runOrReport(gp, "set_current_hit_points('player', g_maxhp * 0.4)");
    runOrReport(gp, "g_hp = get_current_hit_points('player')");
    hp = readLuaGlobalNumber(gp, "g_hp");
    std::printf("health     : set_current_hit_points('player', maxhp*0.4) -> get_current_hit_points reads back %.0f\n",
                hp);
    setLabelText(hHealth, "HEALTH " + std::to_string(static_cast<int>(hp)) + "/" + std::to_string(static_cast<int>(maxHp)));
    renderFrame(outPrefix + "_frame1_damage.png", maxHp > 0 ? hp / maxHp : 0.0);

    // ========================= Frame 2: cash_add toast ====================
    // cash_add (CONFIRMED writer) ends in a generic "post a HUD reward/
    // toast" commit (spec-hud-runtime.md Sec4, 0x0094a800) - CONFIRMED
    // event-driven. Whether the ALWAYS-VISIBLE counter polls separately is
    // explicitly OPEN (Sec4); this overlay polls playerCash() every frame,
    // CHOSEN, not asserted identical to health's own poll pattern.
    runOrReport(gp, "cash_add(500, 'player')");
    const double cashDollars = es.playerCash() / 100.0; // cash_add scales the raw Lua amount x100, Sec4
    std::printf("money      : cash_add(500,'player') (CONFIRMED writer+toast commit, Sec4) -> playerCash() reads back "
                "$%.0f (CHOSEN always-visible poll)\n",
                cashDollars);
    setLabelText(hMoney, "MONEY $" + std::to_string(static_cast<int>(cashDollars)));
    renderFrame(outPrefix + "_frame2_cash.png", maxHp > 0 ? hp / maxHp : 0.0);

    // ============ Frame 3: ammo changes, display does NOT (event-driven) ===
    runOrReport(gp, "inv_weapon_add_temporary('player', 'pistol', 30)");
    std::printf("ammo       : inv_weapon_add_temporary (CONFIRMED writer, Sec3) ran - live ammoCount is now %lld, but\n"
                "             the overlay's displayed text is UNCHANGED (still 'AMMO --') because\n"
                "             game_hud_update_inventory (the CONFIRMED event-driven trigger, Sec3) has not fired yet -\n"
                "             this frame is the negative control proving the cadence is event-driven, not per-frame\n",
                static_cast<long long>(es.weaponTemporaryInventory()["player"]["pistol"].ammoCount));
    renderFrame(outPrefix + "_frame3_ammo_pending.png", maxHp > 0 ? hp / maxHp : 0.0);

    // ========== Frame 4: game_hud_update_inventory fires, display catches up ==========
    const int refreshBefore = es.hudInventoryRefreshCount();
    runOrReport(ui, "game_hud_update_inventory()");
    const int refreshAfter = es.hudInventoryRefreshCount();
    const int64_t ammoCount = es.weaponTemporaryInventory()["player"]["pistol"].ammoCount;
    std::printf("ammo       : game_hud_update_inventory() fired (refresh count %d -> %d) - overlay NOW reads the live\n"
                "             WeaponTemporaryInventorySlot::ammoCount (%lld) into its displayed text\n",
                refreshBefore, refreshAfter, static_cast<long long>(ammoCount));
    setLabelText(hAmmo, "AMMO " + std::to_string(ammoCount));
    renderFrame(outPrefix + "_frame4_ammo_updated.png", maxHp > 0 ? hp / maxHp : 0.0);

    // ========================= Frame 5: notoriety ==========================
    // on_notoriety_event (CONFIRMED to exist, Sec5) - called once to prove
    // the real hook registers; its own consumer is explicitly OPEN (Sec5),
    // so this overlay does NOT gate its refresh on this call (CHOSEN,
    // labelled) - it polls notoriety_set's own written value every frame,
    // same CHOSEN default as health.
    runOrReport(gp, "on_notoriety_event('myHudNotorietyHandler', 'player')");
    runOrReport(gp, "notoriety_set('Police', 3)");
    const double notorietyLevel = es.notorietyAliasTrackStates()[3].currentValue;
    std::printf("notoriety  : on_notoriety_event registered (CONFIRMED hook exists, Sec5, consumer OPEN - not tied to\n"
                "             this overlay's own refresh); notoriety_set('Police',3) (CONFIRMED writer) ->\n"
                "             NotorietyAliasTrackState::currentValue reads back %.0f (CHOSEN poll, same default as "
                "health)\n",
                notorietyLevel);
    setLabelText(hNotoriety, "NOTORIETY LEVEL " + std::to_string(static_cast<int>(notorietyLevel)));
    renderFrame(outPrefix + "_frame5_notoriety.png", maxHp > 0 ? hp / maxHp : 0.0);

    std::printf("done       : 6 frames captured with prefix %s\n", outPrefix.c_str());
    return 0;
}


namespace {

// world (wx,wz) -> minimap-local screen pixel, given the live transform and
// a CHOSEN pixels-per-world-unit scale (see this command's own top doc
// comment: tied to the real, CONFIRMED `zoom` property, but the exact
// pixel-per-unit formula itself is this demo's own CHOSEN visualisation
// choice, not a traced fact). Heading-up convention: the world is rotated
// by -backgroundRotationRad so the player's own forward direction stays
// pointing the same way on screen as heading changes (IDENTICAL formula to
// tests/synthetic_minimap_live_transform_test.cpp's own worldToMinimapScreen(),
// mirrored there since that test has no D3D/window dependency to share this
// function with).
struct MinimapScreenPoint { double x, y; };

MinimapScreenPoint worldToMinimapScreen(double wx, double wz, const sr3luahost::MinimapLiveTransform& t,
                                        double screenCx, double screenCy, double pixelsPerWorldUnit) {
    const double rx = wx - t.mapCentreX, rz = wz - t.mapCentreZ;
    const double c = std::cos(-t.backgroundRotationRad), s = std::sin(-t.backgroundRotationRad);
    const double rotX = rx * c - rz * s, rotZ = rx * s + rz * c;
    return MinimapScreenPoint{screenCx + rotX * pixelsPerWorldUnit, screenCy + rotZ * pixelsPerWorldUnit};
}

} // namespace



int runMinimapDemo(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: sr3_viewer minimap-demo <out_prefix> --fonts <archive.vpp_pc> [--fonts ...]\n"
                     "                   [--textures <archive.vpp_pc>]...\n"
                     "       Corner-minimap live background transform (spec-hud-runtime.md Sec6/Sec6.4): a\n"
                     "       scripted sequence of world positions/headings proving the map background\n"
                     "       pans/rotates while the player's own marker glyph stays fixed at the minimap's\n"
                     "       own centre - see runMinimapDemo()'s own doc comment for the full CONFIRMED/\n"
                     "       CHOSEN/OPEN breakdown. --textures attempts a real `minimap_CCRR` tile texture\n"
                     "       bind (spec-world-streaming.md Sec5); falls back to a flat per-tile colour,\n"
                     "       logged, if none is found (or none given).\n");
        return 1;
    }
    const std::string outPrefix = argv[2];
    std::vector<std::string> fontArchivePaths, textureArchivePaths;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--fonts" && i + 1 < argc) fontArchivePaths.push_back(argv[++i]);
        else if (a == "--textures" && i + 1 < argc) textureArchivePaths.push_back(argv[++i]);
        else { std::fprintf(stderr, "unrecognised argument '%s'\n", a.c_str()); return 2; }
    }
    if (fontArchivePaths.empty()) fontArchivePaths = textureArchivePaths;
    const uint32_t width = 1280, height = 720;

    std::printf("=== minimap-demo: corner-minimap live background transform (spec-hud-runtime.md Sec6/Sec6.4) ===\n");

    sr3render::RenderDevice device;
    std::string error;
    if (!device.initialise(width, height, error)) { std::fprintf(stderr, "device init failed: %s\n", error.c_str()); return 1; }
    std::printf("device     : %s (%s), target %ux%u RGBA8 offscreen\n",
                device.kind() == sr3render::DeviceKind::Hardware ? "HARDWARE" : "WARP (software)",
                device.adapterName().empty() ? "unnamed adapter" : device.adapterName().c_str(), device.width(),
                device.height());

    std::vector<std::unique_ptr<CityArchive>> fontArchivesOwned;
    std::vector<CityArchive*> fontArchives;
    for (const auto& p : fontArchivePaths) {
        auto a = std::make_unique<CityArchive>();
        a->path = p;
        const size_t slash = p.find_last_of("/\\");
        a->label = slash == std::string::npos ? p : p.substr(slash + 1);
        a->bytes = mapFileLargeC(p);
        if (a->bytes.empty()) { std::fprintf(stderr, "could not read %s\n", p.c_str()); return 1; }
        a->top = std::make_unique<vpp::Container>(vpp::ByteView(a->bytes.data(), a->bytes.size()));
        fontArchives.push_back(a.get());
        fontArchivesOwned.push_back(std::move(a));
    }
    HudDemoFont bodyFont;
    {
        const std::string file = std::string(sr3font::gameFontFileStem(0)) + ".vf3_pc";
        std::string parseError;
        for (CityArchive* a : fontArchives) {
            std::vector<uint8_t> bytes, sib;
            if (!findEntry(*a->top, file, bytes, sib, std::string()) || bytes.empty()) continue;
            try {
                bodyFont.font = sr3font::parseFont(bytes);
                bodyFont.loaded = true;
            } catch (const sr3font::FontFormatError& ex) { parseError = ex.what(); }
            break;
        }
        if (!bodyFont.loaded) {
            std::fprintf(stderr, "font %s not found/parsed in any --fonts/--textures archive (%s)\n", file.c_str(),
                         parseError.c_str());
            return 1;
        }
        for (CityArchive* a : fontArchives) {
            sr3render::UploadedTexture ut;
            std::string foundIn;
            if (findAndUploadTexture(*a->top, bodyFont.font.textureName, device.device(), ut, foundIn, &a->texCache)) {
                bodyFont.tex.srv = ut.srv;
                bodyFont.tex.texW = ut.width;
                bodyFont.tex.texH = ut.height;
                break;
            }
        }
        if (!bodyFont.tex.srv) {
            std::fprintf(stderr, "font glyph texture %s not found in any archive\n", bodyFont.font.textureName.c_str());
            return 1;
        }
        std::printf("font       : %s loaded, %u glyphs, glyph texture %s found\n", file.c_str(), bodyFont.font.glyphCount,
                    bodyFont.font.textureName.c_str());
    }

    // ---- --textures archives, for the real `minimap_CCRR` bind attempt
    // (Sec6.3's own OPEN gap) - a SEPARATE archive list from --fonts above
    // (a caller may pass the same paths to both, or only one of the two). --
    std::vector<std::unique_ptr<CityArchive>> texArchivesOwned;
    std::vector<CityArchive*> texArchives;
    for (const auto& p : textureArchivePaths) {
        auto a = std::make_unique<CityArchive>();
        a->path = p;
        const size_t slash = p.find_last_of("/\\");
        a->label = slash == std::string::npos ? p : p.substr(slash + 1);
        a->bytes = mapFileLargeC(p);
        if (a->bytes.empty()) { std::fprintf(stderr, "--textures: could not read %s (skipped)\n", p.c_str()); continue; }
        a->top = std::make_unique<vpp::Container>(vpp::ByteView(a->bytes.data(), a->bytes.size()));
        texArchives.push_back(a.get());
        texArchivesOwned.push_back(std::move(a));
    }

    VintQuadPipeline pipe;
    if (!pipe.initialise(device.device(), width, height, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    device.bindRenderTargetWithDepth(pipe.depthView());
    pipe.begin(device.context(), width, height, false);

    // ---- sr2_map's own 2 real properties (Sec6), through the SAME typed
    // VINT property store every other element type's properties use - NOT
    // a parallel/invented mechanism. map_mode='minimap' (this demo is
    // specifically the corner minimap, not the full-screen pause map, which
    // the SAME element type also backs per Sec6's own finding). ------------
    sr3luahost::EngineState es;
    const uint32_t mapHandle = es.registerTypedVdoObjectForTesting("corner_minimap", "sr2_map", 0, 0);
    {
        sr3luahost::VintTaggedValue z;
        z.kind = sr3luahost::VintTaggedValue::Kind::Number;
        z.number = 1.0; // CHOSEN demo default - see worldToMinimapScreen()'s own doc comment
        es.setVintProperty(mapHandle, "zoom", {z});
        sr3luahost::VintTaggedValue mm;
        mm.kind = sr3luahost::VintTaggedValue::Kind::String;
        mm.text = "minimap";
        es.setVintProperty(mapHandle, "map_mode", {mm});
    }
    const double zoomValue = (*es.getVintProperty(mapHandle, "zoom"))[0].number;
    // map_mode's own GETTER push form is explicitly OPEN for an enum-coded
    // property (spec-lua-bindings.md Sec20.2 - the same refusal
    // `element.auto_offset` already hits, tests/synthetic_vint_property_
    // test.cpp); findVintStoredProperty reads the SETTER's own already-
    // resolved numeric value back directly instead.
    const double mapModeValue = es.findVintStoredProperty(mapHandle, "map_mode")->values[0].number;
    std::printf("sr2_map    : zoom=%.2f (float, this+0xa0, Sec6) map_mode=%.0f (minimap, this+0xa4, Sec6) - real VINT\n"
                "             property store round-trip (EngineState::setVintProperty/findVintStoredProperty)\n",
                zoomValue, mapModeValue);
    // CHOSEN pixels-per-world-unit scale, tied to the real zoom value (see
    // worldToMinimapScreen()'s own doc comment for why the formula itself
    // is CHOSEN, not traced).
    const double kBasePixelsPerWorldUnit = 0.12;
    const double pixelsPerWorldUnit = kBasePixelsPerWorldUnit * zoomValue;

    // ---- the minimap box itself (CHOSEN screen layout, corner placement
    // matching the real game's own corner minimap - Sec6.4's own "corner
    // minimap" framing) -----------------------------------------------------
    const double boxX0 = 1050, boxY0 = 10, boxX1 = 1250, boxY1 = 210;
    const double boxCx = (boxX0 + boxX1) * 0.5, boxCy = (boxY0 + boxY1) * 0.5;
    const double boxHalfW = (boxX1 - boxX0) * 0.5, boxHalfH = (boxY1 - boxY0) * 0.5;

    // ---- 4 CHOSEN world-fixed "landmark" markers, placed near the demo's
    // own frame-0 player position (set below) so they stay visible across
    // the scripted frames - their own on-screen motion between frames is
    // the visible pan/rotate proof (see this command's own top doc
    // comment). Synthetic positions, not real game content. -----------------
    float tile0915X = 0.0f, tile0915Z = 0.0f;
    cityTileCentre(9, 15, tile0915X, tile0915Z); // the SAME real tile this project's own playtest fixtures already use
    struct Landmark { double x, z; uint8_t rgba[4]; };
    const Landmark landmarks[4] = {
        {tile0915X + 150.0, tile0915Z, {230, 90, 70, 255}},
        {tile0915X, tile0915Z + 150.0, {90, 200, 110, 255}},
        {tile0915X - 150.0, tile0915Z, {90, 140, 230, 255}},
        {tile0915X, tile0915Z - 150.0, {230, 200, 70, 255}},
    };

    // Per-(col,row) texture-or-fallback-colour resolution, memoised across
    // frames (findAndUploadTexture already memoises per archive via
    // CityArchive::texCache - this map additionally remembers the PICK
    // across archives, and the fallback colour when none had it).
    std::map<std::pair<int, int>, sr3render::UploadedTexture> boundTileTex;
    std::map<std::pair<int, int>, bool> tileTried;
    auto resolveTile = [&](int col, int row, ID3D11ShaderResourceView** outSrv, uint32_t* outTexW, uint32_t* outTexH,
                           uint8_t fallback[4], bool* wasReal) {
        const auto key = std::make_pair(col, row);
        *outSrv = nullptr;
        *outTexW = *outTexH = 1;
        *wasReal = false;
        if (!tileTried[key]) {
            tileTried[key] = true;
            char nameBuf[32];
            std::snprintf(nameBuf, sizeof nameBuf, "minimap_%02d%02d", col, row);
            for (CityArchive* a : texArchives) {
                sr3render::UploadedTexture ut;
                std::string foundIn;
                if (findAndUploadTexture(*a->top, nameBuf, device.device(), ut, foundIn, &a->texCache)) {
                    boundTileTex[key] = ut;
                    std::printf("minimap_tex: '%s' FOUND in %s (col %d row %d) - real texture bound\n", nameBuf,
                                a->label.c_str(), col, row);
                    break;
                }
            }
            if (boundTileTex.find(key) == boundTileTex.end()) {
                std::printf("minimap_tex: '%s' not found in any of %zu --textures archive(s) (col %d row %d) - "
                            "CHOSEN flat-colour fallback (Sec6.3's own OPEN bind-site gap)\n",
                            nameBuf, texArchives.size(), col, row);
            }
        }
        auto it = boundTileTex.find(key);
        if (it != boundTileTex.end() && it->second.srv) {
            *outSrv = it->second.srv;
            *outTexW = it->second.width;
            *outTexH = it->second.height;
            *wasReal = true;
        }
        tileFallbackColour(col, row, fallback);
    };

    const uint32_t hCaption = es.registerTypedVdoObjectForTesting("minimap_caption", "text", 0, 0);
    const uint32_t hStats = es.registerTypedVdoObjectForTesting("minimap_stats", "text", 0, 0);
    {
        sr3luahost::VintTaggedValue vx, vy;
        vx.kind = sr3luahost::VintTaggedValue::Kind::Number;
        vy.kind = sr3luahost::VintTaggedValue::Kind::Number;
        vx.number = 40; vy.number = 24;
        es.setVintProperty(hCaption, "offset", {vx, vy});
        vx.number = 40; vy.number = 56;
        es.setVintProperty(hStats, "offset", {vx, vy});
    }
    auto setLabel = [&](uint32_t h, const std::string& text) {
        sr3luahost::VintTaggedValue v;
        v.kind = sr3luahost::VintTaggedValue::Kind::String;
        v.text = text;
        es.setVintProperty(h, "text_tag", {v});
    };

    auto renderFrame = [&](const std::string& path, const std::string& caption,
                           const sr3luahost::MinimapLiveInput& in, double displayWorldX, double displayWorldZ) {
        const sr3luahost::MinimapLiveTransform t = sr3luahost::updateMinimapLiveTransform(in);
        int col = 0, row = 0;
        cityTileColRowForWorld(static_cast<float>(t.mapCentreX), static_cast<float>(t.mapCentreZ), col, row);
        ID3D11ShaderResourceView* tileSrv = nullptr;
        uint32_t tileTexW = 1, tileTexH = 1;
        uint8_t fallback[4];
        bool wasReal = false;
        resolveTile(col, row, &tileSrv, &tileTexW, &tileTexH, fallback, &wasReal);

        device.clear({18, 18, 22, 255});

        // Base map layer (real texture when resolveTile() found one, else
        // the flat per-tile fallback colour, Sec6.3's own OPEN bind-site gap,
        // CHOSEN here) - drawMinimapBackgroundQuad(), shared with
        // runPlaytest()'s own live --hud overlay (see that function's own
        // helper doc comment, just above runMinimapDemo(), for why).
        drawMinimapBackgroundQuad(device.context(), pipe, t, boxCx, boxCy, boxHalfW, boxHalfH, tileSrv, tileTexW,
                                  tileTexH, fallback, wasReal);

        // 4 world-fixed landmark markers (CHOSEN, synthetic - not real game
        // content, demo-only): their own motion between frames IS the
        // visible pan/rotate proof.
        for (const Landmark& lm : landmarks) {
            const MinimapScreenPoint sp = worldToMinimapScreen(lm.x, lm.z, t, boxCx, boxCy, pixelsPerWorldUnit);
            pipe.rect(device.context(), sp.x - 4, sp.y - 4, sp.x + 4, sp.y + 4, lm.rgba);
        }

        // The player's own fixed-centre marker - drawMinimapPlayerMarker(),
        // same shared helper runPlaytest() uses.
        drawMinimapPlayerMarker(device.context(), pipe, boxCx, boxCy);

        setLabel(hCaption, caption);
        char statsBuf[160];
        std::snprintf(statsBuf, sizeof statsBuf,
                     "WORLD (%.0f,%.0f) HEADING %.0fDEG STATIC:%s TILE %02d%02d %s", displayWorldX, displayWorldZ,
                     (in.seatedInVehicle ? in.vehicleHeadingRad : in.playerHeadingRad) * 180.0 / 3.14159265, in.staticMinimap ? "ON" : "OFF",
                     col, row, wasReal ? "REAL-TEX" : "FALLBACK");
        setLabel(hStats, statsBuf);
        const uint8_t white[4] = {255, 255, 255, 255};
        drawHudLabel(es, hCaption, bodyFont, pipe, device.context(), white);
        drawHudLabel(es, hStats, bodyFont, pipe, device.context(), white);

        std::vector<uint8_t> pixels;
        if (!device.readBack(pixels, error)) { std::fprintf(stderr, "readback failed: %s\n", error.c_str()); return false; }
        if (!sr3render::writePng(path, width, height, pixels, error)) {
            std::fprintf(stderr, "png write failed: %s\n", error.c_str());
            return false;
        }
        std::printf("wrote      : %s (%s)\n", path.c_str(), caption.c_str());
        return true;
    };

    sr3luahost::MinimapLiveInput in;
    in.playerWorldX = static_cast<double>(tile0915X);
    in.playerWorldZ = static_cast<double>(tile0915Z);
    in.playerHeadingRad = 0.0;
    in.staticMinimap = false;

    // ===== Frame 0: baseline, on foot, static OFF ==========================
    renderFrame(outPrefix + "_frame0_baseline.png", "BASELINE", in, in.playerWorldX, in.playerWorldZ);

    // ===== Frame 1: player moves (same tile) - background PANS =============
    in.playerWorldX += 80.0;
    in.playerWorldZ -= 60.0;
    renderFrame(outPrefix + "_frame1_pan.png", "PAN (moved +80,-60)", in, in.playerWorldX, in.playerWorldZ);

    // ===== Frame 2: player turns - background ROTATES (static OFF) =========
    in.playerHeadingRad = 1.5707963267948966; // 90 deg
    renderFrame(outPrefix + "_frame2_rotate.png", "ROTATE (heading 90deg, static OFF)", in, in.playerWorldX,
               in.playerWorldZ);

    // ===== Frame 3: static minimap ON - heading keeps changing, rotation does NOT ====
    in.playerHeadingRad = 3.4906585039886591; // 200 deg
    in.staticMinimap = true;
    renderFrame(outPrefix + "_frame3_static_on.png", "STATIC MINIMAP ON (heading 200deg, rotation forced to 0)", in,
               in.playerWorldX, in.playerWorldZ);

    // ===== Frame 4: player crosses a tile boundary - a DIFFERENT tile resolves ======
    in.playerWorldX += 400.0; // kCityTilePitchX is 320 - this crosses into the next column
    renderFrame(outPrefix + "_frame4_tile_crossing.png", "TILE CROSSING (+400 X)", in, in.playerWorldX,
               in.playerWorldZ);

    // ===== Frame 5: seated in a vehicle - map-centre follows the VEHICLE, ============
    // not the player's own (now stale) on-foot position (Sec6.4: "falling
    // back to a linked vehicle's position if the player is seated").
    in.staticMinimap = false;
    in.seatedInVehicle = true;
    in.vehicleWorldX = in.playerWorldX + 300.0;
    in.vehicleWorldZ = in.playerWorldZ + 100.0;
    in.vehicleHeadingRad = 0.7853981633974483; // 45 deg
    renderFrame(outPrefix + "_frame5_seated_vehicle.png", "SEATED IN VEHICLE (map-centre follows vehicle)", in,
               in.vehicleWorldX, in.vehicleWorldZ);

    std::printf("done       : 6 frames captured with prefix %s\n", outPrefix.c_str());
    return 0;
}
