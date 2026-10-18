// Synthetic tests for sr3hud. Every expected value comes from the TEXT of
// spec-hud-runtime.md (H.1-H.4, including the H.4 test vectors) and
// spec-world-streaming.md 10.6(g), never from this project's own source.
//
// Style follows tests/synthetic_tables_progression_test.cpp: a tiny
// hand-rolled CHECK runner and "N checks passed." on the last line.

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "sr3hud/dataitem.h"
#include "sr3hud/display_model.h"
#include "sr3hud/minimap.h"
#include "sr3hud/producers.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        ++g_checks;                                                        \
        if (!(cond)) {                                                     \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":"        \
                      << __LINE__ << "\n";                                 \
            ++g_failures;                                                 \
        }                                                                   \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                               \
    do {                                                                    \
        ++g_checks;                                                        \
        const double va_ = (a), vb_ = (b);                                 \
        if (!(std::fabs(va_ - vb_) <= (tol))) {                            \
            std::cerr << "CHECK_NEAR FAILED: " #a " = " << va_ << ", " #b  \
                      << " = " << vb_ << " at " __FILE__ ":" << __LINE__  \
                      << "\n";                                             \
            ++g_failures;                                                 \
        }                                                                   \
    } while (0)

using namespace sr3hud;

constexpr double kPi = 3.14159265358979323846;

void run(const char* name, void (*fn)()) {
    int before = g_failures;
    fn();
    if (g_failures != before) std::cerr << "  (in " << name << ")\n";
}

// ===========================================================================
// H.1 dataitems
// ===========================================================================
void testDataItemChangeDetection() {
    DataItem d("x", {{"a", FieldType::Int}, {"b", FieldType::Float}, {"c", FieldType::String}});
    CHECK(!d.dirty());
    CHECK(!d.set(0, int32_t{0}));  // equal to the default: no store, stays clean
    CHECK(!d.dirty());
    CHECK(d.set(0, int32_t{7}));
    CHECK(d.dirty());
    CHECK(d.consumeDirty());
    CHECK(!d.dirty());
    CHECK(!d.set(0, int32_t{7}));  // same value again
    CHECK(!d.dirty());
    CHECK(!d.set(1, int32_t{1}));  // wrong type: refused
    CHECK(d.getFloat(1) == 0.0f);
    CHECK(!d.set(9, int32_t{1}));  // out of range
    CHECK(d.set(2, std::string("hi")));
    CHECK(d.getString(2) == "hi");
    CHECK(d.fieldIndex("c") == 2);
    CHECK(d.fieldIndex("zz") == -1);
}

void testHudDataItemsShape() {
    HudDataItems h;
    CHECK(h.status.name() == "sr2_local_player_status");
    CHECK(h.status.fieldCount() == 13);
    CHECK(h.status.field(status::HealthPct).name == "health_pct");
    CHECK(h.status.field(status::HealthPct).type == FieldType::Float);
    CHECK(h.status.field(status::HealthLevel).type == FieldType::Int);
    CHECK(h.status.field(status::IsControllingMissile).name == "is_controlling_missile");
    CHECK(h.infrequent.name() == "sr2_local_player_status_infrequent");
    CHECK(h.infrequent.fieldCount() == 9);
    CHECK(h.infrequent.field(infrequent::Cash).type == FieldType::Float);
    CHECK(h.infrequent.field(infrequent::MaxFollowers).type == FieldType::Uint);
    CHECK(h.infrequent.field(infrequent::RadioStation).name == "radio_station");
    CHECK(h.infrequent.field(infrequent::ShowVtolHints).name == "show_vtol_hints");
    CHECK(h.respect.name() == "sr2_local_player_respect");
    CHECK(h.respect.fieldCount() == 5);
    CHECK(h.respect.field(respect::RespectPct).type == FieldType::Float);
    CHECK(h.weapons.name() == "sr2_local_player_weapons");
    CHECK(h.weapons.fieldCount() == 25);
    CHECK(h.weapons.field(weapons::NoMag).name == "no_mag");
    CHECK(h.frequentWeapon.fieldCount() == 8);
    CHECK(h.frequentWeapon.field(frequent_weapon::FineAimTransition).name == "fine_aim_transition");
    for (const DataItem& n : h.notoriety) {
        CHECK(n.fieldCount() == 3);
        CHECK(n.field(notoriety::NotoLevel).type == FieldType::Float);
    }
    CHECK(h.hood.fieldCount() == 4);
    CHECK(h.find("sr2_local_player_respect") == &h.respect);
    CHECK(h.find("nope") == nullptr);
}

// ===========================================================================
// H.2 encodings and producers
// ===========================================================================
void testEncodings() {
    CHECK(HealthPct(75.0f, 150) == 0.5f);         // H.4(b)
    CHECK(HealthPct(10.0f, 0) == 0.0f);
    CHECK(HealthPct(10.0f, -5) == 0.0f);
    CHECK_NEAR(HealthPct(99.99f, 100), 0.999, 1e-6);  // truncates, not rounds
    CHECK(HealthPct(150.0f, 150) == 1.0f);
    CHECK(CashDollars(1234500) == 12345.0f);      // H.4(b)
    CHECK(CashDollars(-250) == -2.5f);

    NotorietyThresholds t{};
    t[2] = 100;      // gang row L=2
    t[5 + 2] = 400;  // police row L=2
    NotorietyTrack rec{2, 200.0f};
    CHECK(NotorietyDisplayLevel(kPoliceTrack, rec, t) == 2.5f);  // H.4(b): police 2.5
    CHECK(NotorietyDisplayLevel(0, rec, t) == 4.0f);              // gang row: 2 + 200/100
    CHECK(NotorietyDisplayLevel(1, {5, 999.0f}, t) == 5.0f);
    CHECK(NotorietyDisplayLevel(3, {7, 0.0f}, t) == 5.0f);
    CHECK(NotorietyDisplayLevel(3, {1, 50.0f}, t) == 1.0f);       // zero threshold guard

    CHECK_NEAR(RespectPct({0, 2, 400.0, 1000}), 0.4, 1e-6);
    CHECK(RespectPct({0, 2, 1000.0, 1000}) == 0.0f);   // exactly full publishes 0.0
    CHECK(RespectPct({0, 49, 5.0, 22000}) == 0.0f);    // capped rank
    CHECK(RespectPct({0, 0, 5.0, 0}) == 0.0f);
    CHECK(RespectPct({0, 0, 5.0, -3}) == 0.0f);
    CHECK_NEAR(RespectPct({0, 0, 1500.0, 1000}), 0.5, 1e-6);
}

void testProducers() {
    HudDataItems h;
    PlayerStatusInput s;
    s.hp = 75.0f;
    s.maxHp = 150;
    PushPlayerStatus(h.status, s);
    CHECK(h.status.getFloat(status::HealthPct) == 0.5f);
    CHECK(h.status.getInt(status::HealthLevel) == 0);
    CHECK(h.status.consumeDirty());
    PushPlayerStatus(h.status, s);  // same frame values: no dirty
    CHECK(!h.status.dirty());
    s.hp = 74.0f;
    PushPlayerStatus(h.status, s);
    CHECK(h.status.dirty());

    PlayerStatusInfrequentInput inf;
    inf.cashCents = 1234500;
    PushPlayerStatusInfrequent(h.infrequent, inf);
    CHECK(h.infrequent.getFloat(infrequent::Cash) == 12345.0f);
    h.infrequent.consumeDirty();
    PushVehicleLogo(h.infrequent, "ui_logo_x");
    CHECK(h.infrequent.getString(infrequent::VehicleLogo) == "ui_logo_x");

    RadioStationProducer radio;
    h.infrequent.consumeDirty();
    CHECK(radio.update(h.infrequent, 42u, "ui_radio_10666_the_blood"));
    CHECK(h.infrequent.getString(infrequent::RadioStation) == "ui_radio_10666_the_blood");
    CHECK(!radio.update(h.infrequent, 42u, "ignored"));  // same record: no push
    CHECK(h.infrequent.getString(infrequent::RadioStation) == "ui_radio_10666_the_blood");
    CHECK(radio.update(h.infrequent, std::nullopt, "ignored"));  // null record -> ""
    CHECK(h.infrequent.getString(infrequent::RadioStation).empty());

    RespectProducer rp;
    rp.requestForceShow(2);
    rp.push(h.respect, {1234, 2, 400.0, 1000}, 2);
    CHECK(h.respect.getInt(respect::TotalRespect) == 1234);
    CHECK_NEAR(h.respect.getFloat(respect::RespectPct), 0.4, 1e-6);
    CHECK(h.respect.getInt(respect::RespectLevel) == 2);
    CHECK(h.respect.getBool(respect::ShowUpgrades));
    CHECK(h.respect.getBool(respect::ForceShowRespect));
    rp.push(h.respect, {1234, 2, 400.0, 1000}, 0);
    CHECK(!h.respect.getBool(respect::ShowUpgrades));
    CHECK(h.respect.getBool(respect::ForceShowRespect));  // counter was 1
    rp.push(h.respect, {1234, 2, 400.0, 1000}, 0);
    CHECK(!h.respect.getBool(respect::ForceShowRespect));
    CHECK(rp.forceShowCounter() == 0);

    std::array<NotorietyTrack, 4> tracks{};
    tracks[3] = {2, 200.0f};
    NotorietyThresholds t{};
    t.fill(100);
    t[7] = 400;
    FakeNotoriety fake;
    PushNotoriety(h.notoriety, tracks, t, fake);
    CHECK(h.notoriety[3].getString(notoriety::TeamName) == "police");
    CHECK(h.notoriety[3].getFloat(notoriety::NotoLevel) == 2.5f);
    CHECK(h.notoriety[3].getString(notoriety::NotoIcon).empty());
    CHECK(h.notoriety[0].getString(notoriety::TeamName) == "brotherhood");
    CHECK(h.notoriety[1].getString(notoriety::TeamName) == "ronin");
    CHECK(h.notoriety[2].getString(notoriety::TeamName) == "samedi");
    fake.active[3] = true;
    fake.value[3] = 4.0f;
    PushNotoriety(h.notoriety, tracks, t, fake);
    CHECK(h.notoriety[3].getFloat(notoriety::NotoLevel) == 4.0f);

    WeaponHudInput w;
    w.weaponBitmap = "ui_hud_inv_pistol";
    w.clip = 13;
    w.reserve = 61;
    w.dualWield = true;
    PushWeapons(h.weapons, w);
    CHECK(h.weapons.getInt(weapons::RdyAmmo) == 6);   // 13 >> 1
    CHECK(h.weapons.getInt(weapons::RsvAmmo) == 30);  // 61 >> 1
    CHECK(h.weapons.getBool(weapons::DualWield));
    w.dualWield = false;
    w.weaponBitmap.reset();
    PushWeapons(h.weapons, w);
    CHECK(h.weapons.getInt(weapons::RdyAmmo) == 13);
    CHECK(h.weapons.getString(weapons::WpnBmpName).empty());
}

// ===========================================================================
// H.2 / H.4(b) display model
// ===========================================================================
void testHealthDisplay() {
    CHECK_NEAR(HealthFillEndAngle(0.5f, 0), 4.721, 1e-4);
    CHECK_NEAR(HealthFillEndAngle(1.0f, 3), 1.2836, 1e-5);
    CHECK_NEAR(HealthFillEndAngle(0.0f, 4), 6.3, 1e-6);

    HealthDisplay hd;  // previous display health 1.0
    HealthUpdate u = hd.update(0.5f, 0);
    CHECK(u.reacted);
    CHECK_NEAR(u.fillEnd, 4.721, 1e-4);
    CHECK_NEAR(u.bgEnd, 4.686, 1e-4);
    CHECK(u.drop);
    CHECK(!u.vignetteAlpha.has_value());  // the test is pct < .5, strict
    CHECK(!u.retract);

    CHECK(!hd.update(0.5005f, 0).reacted);  // |dpct| <= 0.001
    CHECK(hd.update(0.5f, 1).reacted);       // level change alone reacts
    u = hd.update(0.4f, 1);
    CHECK(u.vignetteAlpha.has_value());
    CHECK_NEAR(*u.vignetteAlpha, 0.1, 1e-6);
    u = hd.update(0.2f, 1);
    CHECK_NEAR(*u.vignetteAlpha, 0.5, 1e-6);
    u = hd.update(1.0f, 1);
    CHECK(!u.drop);
    CHECK(u.retract);
}

void testCashDisplay() {
    CHECK(FormatCash(0) == "0");
    CHECK(FormatCash(999) == "999");
    CHECK(FormatCash(1000) == "1,000");
    CHECK(FormatCash(12345) == "12,345");
    CHECK(FormatCash(1234567) == "1,234,567");
    CHECK(FormatCash(100000) == "100,000");
    CHECK(FormatCash(12.9) == "12");
    CHECK(FormatCash(-1234) == "-1,234");

    CashDisplay c;  // previous display $0
    CHECK(c.setCash(12345.0f, false));  // cash_anim plays
    const std::vector<std::string> expected = {
        "$6,172",  "$9,258",  "$10,801", "$11,573", "$11,959", "$12,152", "$12,248",
        "$12,296", "$12,320", "$12,332", "$12,338", "$12,341", "$12,345"};
    for (const std::string& e : expected) {
        c.tick();
        CHECK(c.text() == e);
    }
    c.tick();
    CHECK(c.text() == "$12,345");
    CHECK(!c.setCash(12345.0f, false));
    CHECK(c.setCash(12345.0f, true));   // force_show_cash
    CHECK(c.setCash(12345.9f, false) == false);  // floor unchanged
    CHECK(c.setCash(12000.0f, false));
    c.tick();
    CHECK(c.shown() == 12345 - 173);  // diff -345 -> floor(-172.5) = -173
}

void testNotorietyDisplay() {
    NotorietyDisplay nd;
    NotorietyDisplayUpdate u = nd.update({0.0f, 0.0f, 0.0f, 2.5f});
    CHECK(u.police.lit == 2);
    CHECK(u.police.icons[0] == IconAnim::Add);
    CHECK(u.police.icons[1] == IconAnim::Add);
    CHECK(u.police.icons[2] == IconAnim::Remove);
    CHECK(u.police.icons[3] == IconAnim::Remove);
    CHECK(u.police.icons[4] == IconAnim::Remove);
    CHECK(u.police.top == TopIconAnim::None);  // the fraction does not change the count
    CHECK(u.ringVisible);
    CHECK(u.playExpand);
    CHECK(u.gang.lit == 0);

    u = nd.update({0.0f, 0.0f, 0.0f, 2.7f});
    CHECK(u.police.top == TopIconAnim::Pulse);
    CHECK(u.police.icons[0] == IconAnim::None);
    CHECK(!u.playExpand);
    u = nd.update({0.0f, 0.0f, 0.0f, 2.2f});
    CHECK(u.police.top == TopIconAnim::Decay);
    u = nd.update({1.2f, 3.4f, 0.5f, 3.0f});
    CHECK(u.police.lit == 3);
    CHECK(u.police.icons[2] == IconAnim::Add);
    CHECK(u.police.top == TopIconAnim::None);
    CHECK(u.gang.lit == 3);  // highest of the three gang items
    u = nd.update({0.0f, 0.0f, 0.0f, 0.9f});
    CHECK(!u.ringVisible);
    CHECK(u.playContract);
    CHECK(u.police.icons[0] == IconAnim::Remove);
    CHECK(u.police.icons[2] == IconAnim::Remove);
    CHECK(u.gang.icons[1] == IconAnim::Remove);
    CHECK(nd.update({0.0f, 0.0f, 0.0f, 7.0f}).police.lit == 5);
}

void testRespectDisplay() {
    RespectDisplay rd;
    RespectMeterUpdate u = rd.update(5000, 0.4f, 2);  // H.4(b): level 3 at 40 %
    CHECK(u.levelText == "3");
    CHECK_NEAR(u.meterEnd, 0.55, 1e-5);
    CHECK_NEAR(u.meterBgEnd, 0.52, 1e-5);
    CHECK(!u.gained.has_value());
    u = rd.update(5150, 0.45f, 2);
    CHECK(u.gained.has_value() && *u.gained == 150);
    u = rd.update(5100, 1.5f, 2);
    CHECK(!u.gained.has_value());
    CHECK_NEAR(u.meterEnd, 2.65 - 5.25, 1e-5);  // clamped to 1
}

void testAmmoText() {
    AmmoTextInput a;
    a.rdy = 12;
    a.rsv = 60;
    CHECK(AmmoText(a) == "12/60");  // H.4(b)
    a.noAmmo = true;
    CHECK(AmmoText(a).empty());
    a.noAmmo = false;
    a.noMag = true;
    CHECK(AmmoText(a) == "72");
    a.infAmmo = true;
    CHECK(AmmoText(a) == "[image:ui_hud_base_smcirc_infinite]");
    a.noMag = false;
    CHECK(AmmoText(a) == "12/[image:ui_hud_base_smcirc_infinite]");
    a.infAmmo = false;
    a.contFire = true;
    a.rdy = 1250;
    a.rsv = 399;
    CHECK(AmmoText(a) == "12/3");
    CHECK(GrenadeText(0).empty());
    CHECK(GrenadeText(3) == "3");
    CHECK(WeaponIconBitmap("") == "ui_hud_inv_fist");
    CHECK(WeaponIconBitmap("ui_hud_inv_pistol") == "ui_hud_inv_pistol");
    CHECK(MinimapGroupAlpha(0.0f) == 1.0f);
    CHECK_NEAR(MinimapGroupAlpha(0.7f), 0.5, 1e-6);
    CHECK(MinimapGroupAlpha(1.5f) == 0.0f);
}

// ===========================================================================
// H.3 / H.4(a) minimap
// ===========================================================================
struct Landmark {
    const char* name;
    double x, z;
    const char* tile;
    double xmin, xmax, zmin, zmax, u, v, tu, tv;
};

const Landmark kLandmarks[] = {
    {"Planet_saints_gps", 1704.3511, -466.8722, "1815", 1280, 1920, -816, -304,
     0.336951, 0.681890, 215.65, 349.13},
    {"Crib_Kinzie", 2125.9683, -271.3368, "1914", 1760, 2400, -536, -24,
     0.428175, 0.516920, 274.03, 264.66},
    {"Dock_Purchase", 431.2953, 936.4601, "1410", 160, 800, 584, 1096,
     0.576101, 0.688399, 368.70, 352.46},
};

void testTileLattice() {
    for (const Landmark& l : kLandmarks) {
        const TileCoord t = CityTileAt(l.x, l.z);
        CHECK(InCityGrid(t));
        CHECK(CityTileName(t) == l.tile);
        const WorldBox b = MinimapPaddedBounds(CityTileBox(t));
        CHECK_NEAR(b.minX, l.xmin, 1e-9);
        CHECK_NEAR(b.maxX, l.xmax, 1e-9);
        CHECK_NEAR(b.minZ, l.zmin, 1e-9);
        CHECK_NEAR(b.maxZ, l.zmax, 1e-9);
        CHECK_NEAR(b.width(), 640.0, 1e-9);
        CHECK_NEAR(b.height(), 512.0, 1e-9);
        const MinimapBaseTexture tex =
            SelectMinimapBaseTexture(true, CityTileName(t), CityTileBox(t), true, false, {});
        CHECK(tex.bitmap == std::string("minimap_") + l.tile + ".tga");
        const Vec2 uv = MinimapTextureUV(tex.bounds, l.x, l.z);
        CHECK_NEAR(uv.x, l.u, 1e-6);
        CHECK_NEAR(uv.y, l.v, 1e-6);
        CHECK_NEAR(uv.x * 640.0, l.tu, 0.005);
        CHECK_NEAR(uv.y * 512.0, l.tv, 0.005);
    }
    // Brick stagger: centre of tile (col, row) maps back to itself on both
    // parities, and the lower box edges belong to the tile.
    for (TileCoord t : {TileCoord{10, 10}, TileCoord{10, 11}, TileCoord{3, 22}}) {
        const WorldBox b = CityTileBox(t);
        TileCoord c = CityTileAt(b.centreX(), b.centreZ());
        CHECK(c.col == t.col && c.row == t.row);
        c = CityTileAt(b.minX, b.minZ);
        CHECK(c.col == t.col && c.row == t.row);
        c = CityTileAt(b.maxX, b.centreZ());
        CHECK(c.col == t.col + 1 && c.row == t.row);
    }
    CHECK(CityTileBox({0, 0}).centreX() == -4000.0);
    CHECK(CityTileBox({0, 1}).centreX() == -4160.0);
    CHECK(CityTileBox({0, 1}).centreZ() == 3360.0);
    CHECK(!InCityGrid({25, 3}));
    CHECK(!InCityGrid(CityTileAt(0.0, 9000.0)));

    // Fallbacks.
    const WorldBox world{-5000, -4000, 5000, 4000};
    MinimapBaseTexture t =
        SelectMinimapBaseTexture(true, "1815", CityTileBox({18, 15}), false, false, world);
    CHECK(t.bitmap == "sr2_water_minimap.tga");
    CHECK_NEAR(t.bounds.width(), 640.0, 1e-9);
    t = SelectMinimapBaseTexture(false, "", {}, true, false, world);
    CHECK(t.bitmap == "mapsaintsrow.tga");
    CHECK(t.bounds.maxX == 5000.0 && t.bounds.minZ == -4000.0);
}

void testSpeedZoom() {
    CHECK_NEAR(MinimapZoom(0), 0.75, 1e-12);
    CHECK_NEAR(MinimapZoom(40), 0.675, 1e-12);
    CHECK_NEAR(MinimapZoom(100), 0.5625, 1e-12);
    CHECK_NEAR(MinimapZoom(150), 0.5625, 1e-12);
    CHECK_NEAR(MinimapScale(0.75, false, 1.0), 0.75, 1e-12);
    CHECK_NEAR(MinimapScale(0.75, true, 1.0), 0.75 * 1.75, 1e-12);
    CHECK_NEAR(MinimapScale(0.75, false, 1.5), 1.125, 1e-12);

    // Visible radius at the ring: 89.3 m at rest, 99.3 m at 40 mph, 119.1 m
    // at 100 mph or more.
    CHECK_NEAR(67.0 / MinimapScale(MinimapZoom(0), false, 1.0), 89.3, 0.05);
    CHECK_NEAR(67.0 / MinimapScale(MinimapZoom(40), false, 1.0), 99.3, 0.05);
    CHECK_NEAR(67.0 / MinimapScale(MinimapZoom(100), false, 1.0), 119.1, 0.05);

    const double mps = 1.0 / kMinimapMphPerMetrePerSecond;  // 1 mph
    MinimapSpeedZoom z;
    z.update(0.0, 1.0 / 60);
    CHECK(z.level() == 0);
    z.update(60 * mps, 1.0 / 60);  // speed rises: snaps at once
    CHECK(z.level() == 60);
    z.update(57 * mps, 1.0 / 60);  // within 5 of the held peak: holds
    CHECK(z.level() == 60);
    CHECK(z.heldTarget() == 60);
    z.update(130 * mps, 1.0 / 60);  // capped at 100
    CHECK(z.level() == 100);
    CHECK(z.heldTarget() == 130);
    // Stop: T drops to 0, s falls at 13 per second.
    for (int i = 0; i < 60; ++i) z.update(0.0, 1.0 / 60);
    CHECK(z.heldTarget() == 0);
    CHECK(z.level() >= 87 && z.level() <= 88);
    for (int i = 0; i < 60 * 10; ++i) z.update(0.0, 1.0 / 60);
    CHECK(z.level() == 0);
    // A big dt steps by floor(13*dt), keeping the remainder.
    MinimapSpeedZoom y;
    y.update(50 * mps, 0.0);
    y.update(0.0, 1.0);  // acc 13 -> step 13
    CHECK(y.level() == 37);
    CHECK_NEAR(y.accumulator(), 0.0, 1e-12);
    y.update(0.0, 0.1);  // acc 1.3 -> step 1, keep 0.3
    CHECK(y.level() == 36);
    CHECK_NEAR(y.accumulator(), 0.3, 1e-9);
}

void testRotationAndBlips() {
    const Vec2 C = kMinimapCentreRef;
    CHECK(C.x == 191.0 && C.y == 566.0);
    const Vec2 P{1704.3511, -466.8722};  // player at Planet_saints_gps, on foot
    const double k = MinimapScale(MinimapZoom(0), false, 1.0);

    // Camera headings: facing +X -> h = 90 deg; facing +Z -> h = 0.
    const double h90 = HeadingFromForward(1.0, 0.0);
    const double h0 = HeadingFromForward(0.0, 1.0);
    CHECK_NEAR(h90, kPi / 2, 1e-12);
    CHECK_NEAR(h0, 0.0, 1e-12);
    const MinimapRotation rot90 = ComputeMinimapRotation(false, h90, 0.0);
    const MinimapRotation rot0 = ComputeMinimapRotation(false, h0, 0.0);
    const MinimapRotation rotS = ComputeMinimapRotation(true, h90, 0.3);
    CHECK_NEAR(rot90.theta, -(kPi / 2 + kPi), 1e-12);
    CHECK(rotS.mapTheta == 0.0);
    CHECK_NEAR(rotS.glyphRotation, 0.3 + kPi, 1e-12);
    CHECK(rot90.glyphRotation == 0.0);

    struct Case {
        double dx, dz, kk;
        Vec2 r90, r0, st;
        bool clamped;
    };
    const double kFast = MinimapScale(MinimapZoom(100), false, 1.0);
    const Case cases[] = {
        {0, -100, k, {67, 0}, {0, 67}, {0, -67}, true},
        {100, 0, k, {0, -67}, {67, 0}, {-67, 0}, true},
        {0, -60, k, {45, 0}, {0, 45}, {0, -45}, false},
        {0, -100, kFast, {56.25, 0}, {0, 56.25}, {0, -56.25}, false},
    };
    for (const Case& c : cases) {
        const double X = P.x + c.dx, Z = P.y + c.dz;
        const MinimapRotation* rs[3] = {&rot90, &rot0, &rotS};
        const Vec2 want[3] = {c.r90, c.r0, c.st};
        for (int i = 0; i < 3; ++i) {
            const BlipPlacement b = PlaceMinimapBlip(C, P, X, Z, c.kk, rs[i]->mapTheta, 1.0);
            CHECK(b.clamped == c.clamped);
            CHECK_NEAR(b.screen.x - C.x, want[i].x, 1e-9);
            CHECK_NEAR(b.screen.y - C.y, want[i].y, 1e-9);
            if (!c.clamped) {  // the cartesian form agrees inside the ring
                const Vec2 s = MinimapWorldToScreen(C, P, X, Z, c.kk, rs[i]->mapTheta);
                CHECK_NEAR(s.x, b.screen.x, 1e-9);
                CHECK_NEAR(s.y, b.screen.y, 1e-9);
            }
        }
    }
    const BlipPlacement e = PlaceMinimapBlip(C, P, P.x, P.y - 100, k, 0.0, 1.0);
    CHECK_NEAR(e.edge, 0.0597, 1e-4);
    CHECK(PlaceMinimapBlip(C, P, P.x, P.y - 10, k, 0.0, 1.0).edge == 0.0);
    CHECK(PlaceMinimapBlip(C, P, P.x, P.y - 10000, k, 0.0, 1.0).edge == 1.0);
    // UI scale scales the clamp radius.
    const BlipPlacement big = PlaceMinimapBlip(C, P, P.x, P.y - 1000, k * 2, 0.0, 2.0);
    CHECK_NEAR(big.screen.y - C.y, -134.0, 1e-9);

    // North marker: top of the ring in static mode, -Z direction always.
    const Vec2 n = MinimapNorthMarker(C, 0.0, 1.0);
    CHECK_NEAR(n.x, 191.0, 1e-9);
    CHECK_NEAR(n.y, 499.0, 1e-9);
    const Vec2 n90 = MinimapNorthMarker(C, rot90.mapTheta, 1.0);
    CHECK_NEAR(n90.x - C.x, 67.0, 1e-9);
    CHECK_NEAR(n90.y - C.y, 0.0, 1e-9);

    // In rotating mode the camera's forward direction is screen up.
    const double fx = 0.6, fz = -0.8;
    const MinimapRotation rf = ComputeMinimapRotation(false, HeadingFromForward(fx, fz), 0.0);
    const Vec2 ahead = MinimapWorldToScreen(C, P, P.x + 50 * fx, P.y + 50 * fz, k, rf.mapTheta);
    CHECK_NEAR(ahead.x - C.x, 0.0, 1e-9);
    CHECK_NEAR(ahead.y - C.y, -37.5, 1e-9);
}

void testCentreAndBaseQuad() {
    MinimapCentreInput ci;
    ci.playerX = 1;
    ci.playerZ = 2;
    ci.vehicleX = 3;
    ci.vehicleZ = 4;
    ci.cameraX = 5;
    ci.cameraZ = 6;
    CHECK(MinimapCentre(ci).x == 1 && MinimapCentre(ci).y == 2);
    ci.inVehicle = true;
    CHECK(MinimapCentre(ci).x == 3 && MinimapCentre(ci).y == 4);
    ci.specialCameraMode = true;
    CHECK(MinimapCentre(ci).x == 5 && MinimapCentre(ci).y == 6);

    // C-2 vector: player at Planet_saints_gps on foot, the screen centre C
    // shows the player's own UV (0.336951, 0.681890) of minimap_1815, in any
    // rotation.
    const Vec2 C = kMinimapCentreRef;
    const Vec2 P{1704.3511, -466.8722};
    const double k = 0.75;
    const TileCoord t = CityTileAt(P.x, P.y);
    const MinimapBaseTexture tex =
        SelectMinimapBaseTexture(true, CityTileName(t), CityTileBox(t), true, false, {});
    for (double theta : {0.0, -1.5 * kPi, -kPi, 0.7}) {
        const MinimapQuad q = MinimapBaseQuad(C, P, tex, k, theta);
        CHECK_NEAR(q.width, 480.0, 1e-9);
        CHECK_NEAR(q.height, 384.0, 1e-9);
        CHECK(q.rotation == theta);
        // Undo the quad's rotation to find C in the quad's own frame.
        const double dx = C.x - q.centre.x, dy = C.y - q.centre.y;
        const double c = std::cos(-theta), s = std::sin(-theta);
        const double lx = c * dx - s * dy, ly = s * dx + c * dy;
        CHECK_NEAR(0.5 + lx / q.width, 0.336951, 1e-6);
        CHECK_NEAR(0.5 + ly / q.height, 0.681890, 1e-6);
    }

    // Interior: the shipped reference point is the world origin.
    MinimapBaseTexture in = tex;
    in.interior = true;
    const MinimapQuad qi = MinimapBaseQuad(C, P, in, k, 0.0);
    CHECK_NEAR(qi.centre.x, C.x + k * P.x, 1e-9);
    CHECK_NEAR(qi.centre.y, C.y - k * P.y, 1e-9);
    const MinimapQuad qc = MinimapBaseQuad(C, P, in, k, 0.0, true);
    const MinimapQuad qe = MinimapBaseQuad(C, P, tex, k, 0.0);
    CHECK_NEAR(qc.centre.x, qe.centre.x, 1e-9);
    CHECK_NEAR(qc.centre.y, qe.centre.y, 1e-9);
}

void testCategoryPasses() {
    CHECK(MinimapCategoryPassEnabled(9, 0));
    CHECK(!MinimapCategoryPassEnabled(10, 0));
    CHECK(MinimapCategoryPassEnabled(10, 0x4));
    CHECK(MinimapCategoryPassEnabled(8, 0x2));
    CHECK(MinimapCategoryPassEnabled(7, 0x1));
    CHECK(MinimapCategoryPassEnabled(0, 0x8));
    CHECK(MinimapCategoryPassEnabled(2, 0x40));
    CHECK(MinimapCategoryPassEnabled(3, 0x80));
    CHECK(!MinimapCategoryPassEnabled(3, 0x7F));
    CHECK(!MinimapCategoryPassEnabled(5, 0xFFFFFFFFu));
}

void testReviewAdditions() {
    CHECK(MinimapZoomSourceSpeed(12.5, 30.0) == 12.5);
    CHECK(MinimapZoomSourceSpeed(std::nullopt, 30.0) == 30.0);
    CHECK(MinimapZoomSourceSpeed(std::nullopt, std::nullopt) == 0.0);
    HudDataItems h;
    WeaponHudInput w;
    w.clip = 1;
    w.reserve = 9;
    w.clipForcedZero = true;
    PushWeapons(h.weapons, w);
    CHECK(h.weapons.getInt(weapons::RdyAmmo) == 0);
    CHECK(h.weapons.getInt(weapons::RsvAmmo) == 9);
}

}  // namespace

int main() {
    run("testDataItemChangeDetection", testDataItemChangeDetection);
    run("testHudDataItemsShape", testHudDataItemsShape);
    run("testEncodings", testEncodings);
    run("testProducers", testProducers);
    run("testHealthDisplay", testHealthDisplay);
    run("testCashDisplay", testCashDisplay);
    run("testNotorietyDisplay", testNotorietyDisplay);
    run("testRespectDisplay", testRespectDisplay);
    run("testAmmoText", testAmmoText);
    run("testTileLattice", testTileLattice);
    run("testSpeedZoom", testSpeedZoom);
    run("testRotationAndBlips", testRotationAndBlips);
    run("testCentreAndBaseQuad", testCentreAndBaseQuad);
    run("testCategoryPasses", testCategoryPasses);
    run("testReviewAdditions", testReviewAdditions);
    if (g_failures != 0) {
        std::cerr << g_failures << " of " << g_checks << " checks FAILED.\n";
        return 1;
    }
    std::cout << g_checks << " checks passed.\n";
    return 0;
}
