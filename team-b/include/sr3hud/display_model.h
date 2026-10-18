// HUD display model - the documented behaviour of the shipped HUD scripts
// (hud.lua, hud_noto.lua, vdo_respect_meter.lua) for each core stat, as
// spec-hud-runtime.md H.2 states it, with the H.4(b) sample state as test
// vectors. This is what a renderer needs when it draws the HUD itself; a
// host that runs the real hud.lua against the dataitems (H.5's preferred
// wiring) does not need it.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace sr3hud {

// ---------------------------------------------------------------------------
// Health ring (hud_player_status_change)
// ---------------------------------------------------------------------------

constexpr float kHealthRingStartAngle = 6.3f;
// End angle by health_level 0-4. Levels outside 0-4 are clamped (CHOSEN).
constexpr std::array<float, 5> kHealthRingEndAngle = {3.142f, 2.6774f, 2.2128f, 1.2836f, 0.819f};

float HealthFillEndAngle(float pct, int32_t level);  // (end - start) * pct + start

struct HealthUpdate {
    bool reacted = false;      // false: |dpct| <= 0.001 and the level is unchanged
    float fillEnd = 0.0f;      // health_bar.end_angle
    float bgEnd = 0.0f;        // fillEnd - 0.035
    bool drop = false;         // a drop: health_bar_2.start_angle = fillEnd, decay tween runs
    // Vignette fade target: 0.5 - pct below 0.5, 0.70 - pct below 0.3. The
    // spec does not say what the script does at pct >= 0.5; nullopt there.
    std::optional<float> vignetteAlpha;
    bool retract = false;      // pct == 1: the bar retracts after the timer
};

class HealthDisplay {
public:
    // H.4(b) starts from a previous display health of 1.0.
    explicit HealthDisplay(float initialPct = 1.0f, int32_t initialLevel = 0)
        : pct_(initialPct), level_(initialLevel) {}
    HealthUpdate update(float pct, int32_t level);
    float pct() const { return pct_; }

private:
    float pct_;
    int32_t level_;
};

// ---------------------------------------------------------------------------
// Cash counter (hud_player_status_inf_change + the hud_process thread)
// ---------------------------------------------------------------------------

// game_format_int_to_string: floor through a float, a comma every three
// digits, no locale. A negative value keeps its sign in front (CHOSEN; the
// spec does not show a negative case).
std::string FormatCash(double value);

class CashDisplay {
public:
    // Store floor(cash); returns true when cash_anim plays (the value
    // changed, or forceShow is set).
    bool setCash(float cash, bool forceShow);
    // Once per frame: diff = target - shown; if |diff| > 5, diff =
    // floor(diff * 0.5); shown += diff.
    void tick();
    int64_t shown() const { return shown_; }
    int64_t target() const { return target_; }
    std::string text() const { return "$" + FormatCash(static_cast<double>(shown_)); }

private:
    bool primed_ = false;
    int64_t target_ = 0;
    int64_t shown_ = 0;
};

// ---------------------------------------------------------------------------
// Notoriety icons (hud_noto_change)
// ---------------------------------------------------------------------------

constexpr int kNotorietyIconCount = 5;

enum class IconAnim : uint8_t { None, Add, Remove };
enum class TopIconAnim : uint8_t { None, Pulse, Decay };

struct NotorietyIconsUpdate {
    int lit = 0;  // floor(level), clamped to 0-5
    std::array<IconAnim, kNotorietyIconCount> icons{};  // icon _1 .. _5
    TopIconAnim top = TopIconAnim::None;  // fractional change with the same count
};

// One icon row (police, or the gang row). The first update after
// construction plays add for every lit icon and remove for every other one.
class NotorietyIconRow {
public:
    NotorietyIconsUpdate update(float level);
    int lit() const { return lit_; }

private:
    bool primed_ = false;
    int lit_ = 0;
    float level_ = 0.0f;
};

struct NotorietyDisplayUpdate {
    NotorietyIconsUpdate police;
    NotorietyIconsUpdate gang;
    bool ringVisible = false;
    bool playExpand = false;    // map_noteriety_expand
    bool playContract = false;  // map_noteriety_contract
};

// Items in sr2_notoriety order: 0-2 gangs, 3 police. The gang row shows the
// highest of the three gang levels; the ring hides when every type is
// below 1.
class NotorietyDisplay {
public:
    NotorietyDisplayUpdate update(const std::array<float, 4>& levels);

private:
    NotorietyIconRow police_;
    NotorietyIconRow gang_;
    bool ringVisible_ = false;
};

// ---------------------------------------------------------------------------
// Respect meter (hud_player_respect_change -> Vdo_respect_meter)
// ---------------------------------------------------------------------------

struct RespectMeterUpdate {
    std::string levelText;       // respect_level_text: respect_level + 1
    float meterEnd = 0.0f;       // respect_meter.end_angle = 2.65 - 5.25 * clamp(pct, 0, 1)
    float meterBgEnd = 0.0f;     // meterEnd - 0.03
    std::optional<int32_t> gained;  // "+N" when total rose
};

class RespectDisplay {
public:
    RespectMeterUpdate update(int32_t total, float pct, int32_t level);

private:
    bool primed_ = false;
    int32_t prevTotal_ = 0;
};

// ---------------------------------------------------------------------------
// Ammo / weapon icon text (hud_player_weapon_change)
// ---------------------------------------------------------------------------

constexpr const char* kInfiniteAmmoImage = "[image:ui_hud_base_smcirc_infinite]";
constexpr const char* kFistBitmap = "ui_hud_inv_fist";

struct AmmoTextInput {
    int32_t rdy = 0;
    int32_t rsv = 0;
    bool noAmmo = false;
    bool infAmmo = false;
    bool contFire = false;
    bool noMag = false;
};

// no ammo -> ""; no magazine -> rdy+rsv (or the infinite image); continuous
// fire -> floor(rdy/100) "/" floor(rsv/100); infinite reserve ->
// "rdy/[image:...]"; otherwise "rdy/rsv". Where a weapon carries both the
// continuous-fire and infinite flags, continuous fire is tested first
// (CHOSEN order; the spec lists the cases without a precedence).
std::string AmmoText(const AmmoTextInput& in);
// "" when the count is 0, else the count.
std::string GrenadeText(int32_t count);
// An empty published name (no weapon) shows the fist.
std::string WeaponIconBitmap(const std::string& published);

// map_grp.alpha = clamp(1.2 - fine_aim_transition, 0, 1).
float MinimapGroupAlpha(float fineAimTransition);

}  // namespace sr3hud
