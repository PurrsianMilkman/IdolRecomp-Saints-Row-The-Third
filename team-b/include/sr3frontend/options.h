// Options menu for sr3.exe (Team C, HANDOFF-C.md section 10): the PC options list and pages of
// spec-tables-ui-controls.md Sec18.2 over the real profile options struct (spec-save-format.md
// Sec7.5, sr3save::ProfileOptions), and its persistence in an `sr3def_profile` image.
//   * Options list (PC): Mouse, Gameplay, Display, Audio (Sec18.5 vector 4).
//   * Values change as the cursor moves left/right; leaving a page commits (pause_menu_accept_options),
//     and the Gameplay page's difficulty is applied once, when leaving it (Sec18.4, values > 2 ignored).
//   * Every page except Display has Restore Defaults, confirmed by a dialog.
// CHOSEN (HANDOFF-C 10): the English item labels; the mouse-sensitivity UI scale (ui = v / 0.48 x 100,
// so the shipped 0.24 shows 50 - the real (v - c0) / c1 constants are not given); brightness UI
// 0..100 = 0.0..2.0; "airplane controls" bit1 set = Normal (the shipped default Advanced has it clear);
// the Display page holds only the profile items (Brightness, Gameplay VSync, Minimap View) - the
// display.ini items (resolution, preset, ...) and Controls / Remap sub-pages are not built yet.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sr3frontend/bindings.h"
#include "sr3game/frontend.h"
#include "sr3save/save_profile.h"

namespace sr3frontend {

// The shipped defaults (Sec18.2 "default seen in a shipped profile", Sec7.5 real values).
sr3save::ProfileOptions DefaultProfileOptions();

// Writes the options struct into a profile image: the settings magic 0x10 at 0x2000 and the 100-byte
// struct at 0x2004 (Sec7.5). An image of the wrong size is replaced by a new zeroed 0x4248-byte image
// whose header says a 0x400 settings block is present (0x3008) and the binding tables are not
// (profile-valid 0), so the game would keep its default bindings.
std::vector<uint8_t> WriteProfileOptions(std::vector<uint8_t> image, const sr3save::ProfileOptions& o);
// The options struct of a profile image, if it parses and its version is 5.
std::optional<sr3save::ProfileOptions> ReadProfileOptions(const std::vector<uint8_t>& image);
// `<dir>/sr3def_profile` (CHOSEN location: next to the saves).
std::string ProfilePath(const std::string& dir);
sr3save::ProfileOptions LoadOptions(const std::string& dir);  // defaults when absent or unreadable
bool SaveOptions(const std::string& dir, const sr3save::ProfileOptions& o);

enum class OptionsPage { Mouse, Gameplay, Display, Audio };
enum class OptionKind { Toggle, Choice, Slider, Action };
enum class OptionId {
    MouseInvertY, MouseInvertX, MouseSensV, MouseSensH, VehicleCameraSnap,
    Difficulty, AirplaneControls, Subtitles, Radio, MinimapView, PauseOnFocusLoss,
    Brightness, GameplayVsync,
    Sfx, Music, Voice,
    RestoreDefaults,
    RemapControls,  // Mouse page -> the rebinding screens (Sec18.3)
};
struct OptionItem {
    OptionId id;
    std::string label;
    OptionKind kind;
    std::vector<std::string> choices;  // Toggle: No/Yes; Choice: the names
    int step = 1;                      // Slider step (0..100)
};
std::vector<OptionItem> OptionItems(OptionsPage page);
const char* OptionsPageName(OptionsPage page);

// Remap: RemapCategories (a Restore Defaults row, then General .. Tank) -> RemapRows (left/right = primary /
// secondary column) -> RemapCapture ("press any key": the next accepted key or mouse button is bound; Esc
// cancels - CHOSEN). ConfirmRemapReset confirms restoring every set (the retail X button).
enum class OptionsScreen { List, Page, ConfirmRestore, RemapCategories, RemapRows, RemapCapture, ConfirmRemapReset };

class OptionsModel {
public:
    OptionsModel(sr3save::ProfileOptions o = DefaultProfileOptions(), int difficulty = 1);
    void opened();  // back to the list
    // Returns true when the player backs out of the options list (the caller closes the screen).
    bool update(const sr3game::FrontendInput& in);

    OptionsScreen screen() const { return screen_; }
    OptionsPage page() const { return page_; }
    int selection() const { return sel_; }      // list / confirm
    int pageSelection() const { return pageSel_; }  // the row on a page
    int value(OptionId id) const;             // the UI value (toggle 0/1, choice index, slider 0..100)
    std::string valueText(const OptionItem& item) const;
    void setValue(OptionId id, int v);        // clamps; updates the struct live
    const sr3save::ProfileOptions& options() const { return o_; }
    int difficulty() const { return difficulty_; }
    // Commits since the last call (a page was left): the caller saves the profile.
    bool takeCommitted();
    // Rebinding: the binding sets (absent: the Remap item says so and does nothing).
    void setBindings(BindingModel b) { bindings_ = std::move(b); }
    const std::optional<BindingModel>& bindings() const { return bindings_; }
    int remapCategory() const { return remapCat_; }
    int remapRow() const { return remapRow_; }
    bool remapSecondary() const { return remapCol_ == 1; }
    // The captured input while in RemapCapture; returns true when it was bound.
    bool capture(const BindInput& in);

private:
    void restoreDefaults(OptionsPage page);
    sr3save::ProfileOptions o_;
    int difficulty_ = 1;
    int pendingDifficulty_ = 1;
    OptionsScreen screen_ = OptionsScreen::List;
    OptionsPage page_ = OptionsPage::Mouse;
    int sel_ = 0;
    int pageSel_ = 0;
    bool committed_ = false;
    std::optional<BindingModel> bindings_;
    int remapCat_ = 0, remapRow_ = 0, remapCol_ = 0;
};

}  // namespace sr3frontend
