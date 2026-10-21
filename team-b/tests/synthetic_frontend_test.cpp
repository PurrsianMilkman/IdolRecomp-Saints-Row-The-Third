// sr3frontend menu models and gamepad edges (HANDOFF-C.md section 10): main menu with the
// Continue rule of spec-frontend-menu-runtime.md Sec3, the load list, the quit confirm, and the
// pause menu actions of include/sr3game/frontend.h.
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "sr3frontend/bindings.h"
#include "sr3frontend/gamepad.h"
#include "sr3frontend/menu_model.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                                             \
    do {                                                                                        \
        ++g_checks;                                                                             \
        if (!(cond)) {                                                                          \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__ << "\n";        \
            ++g_failures;                                                                       \
        }                                                                                       \
    } while (0)

using namespace sr3frontend;
using sr3game::FrontendInput;
using sr3game::MainMenuResult;
using sr3game::PauseMenuAction;

FrontendInput Up() { FrontendInput i; i.up = true; return i; }
FrontendInput Down() { FrontendInput i; i.down = true; return i; }
FrontendInput Right() { FrontendInput i; i.right = true; return i; }
FrontendInput Left() { FrontendInput i; i.left = true; return i; }
FrontendInput Accept() { FrontendInput i; i.accept = true; return i; }
FrontendInput Back() { FrontendInput i; i.back = true; return i; }

sr3game::SaveFileInfo Save(const char* name, int64_t t, bool valid) {
    sr3game::SaveFileInfo s;
    s.path = std::string("C:/saves/") + name + ".sr3s_pc";
    s.displayName = name;
    s.lastWriteUnix = t;
    s.valid = valid;
    return s;
}

void testContinue() {
    MainMenuModel m;  // no saves: no Continue entry (spec-frontend-flow Sec3.4)
    CHECK(m.continueIndex() == -1 && m.entries().size() == 3 && m.entries()[0] == MainEntry::Campaign &&
          m.entries()[1] == MainEntry::Options);
    // The newest READABLE save, whatever the listing order; unreadable rows sort last.
    m.setSaves({Save("sr3save_01", 100, true), Save("sr3save_03", 300, false), Save("sr3save_02", 200, true)});
    CHECK(m.entries().size() == 4 && m.entries()[0] == MainEntry::Continue);
    CHECK(m.saves()[0].displayName == "sr3save_02" && m.saves()[2].displayName == "sr3save_03");
    const auto r = m.update(Accept());
    CHECK(r && r->kind == MainMenuResult::Kind::LoadSave && r->savePath == "C:/saves/sr3save_02.sr3s_pc");
    MainMenuModel bad;  // only unreadable saves: no Continue
    bad.setSaves({Save("x", 5, false)});
    CHECK(bad.entries()[0] == MainEntry::Campaign);
}

void testNewGame() {
    MainMenuModel m;
    CHECK(!m.update(Accept()) && m.screen() == MainScreen::Campaign);  // Campaign
    CHECK(!m.update(Accept()) && m.screen() == MainScreen::Difficulty && m.selection() == 1);  // cursor on Normal
    CHECK(!m.update(Back()) && m.screen() == MainScreen::Campaign && m.selection() == 0);
    m.update(Accept());
    m.update(Down());
    const auto r = m.update(Accept());
    CHECK(r && r->kind == MainMenuResult::Kind::NewGame && m.difficulty() == 2);  // Hardcore
    MainMenuModel w;
    w.update(Up());  // wraps to Quit Game
    CHECK(w.selection() == 2 && w.entries()[2] == MainEntry::Quit);
    w.update(Down());
    w.update(Accept());
    w.update(Back());  // Campaign -> Top on Campaign
    CHECK(w.screen() == MainScreen::Top && w.selection() == 0);
}

void testLoadList() {
    MainMenuModel m;
    std::vector<sr3game::SaveFileInfo> saves;
    for (int i = 1; i <= 12; ++i) saves.push_back(Save(("sr3save_" + std::to_string(i)).c_str(), i, i != 11));
    m.setSaves(saves);  // 12, 10, 9, ..., 1, then 11 (unreadable) last
    m.update(Down());   // Continue -> Campaign
    m.update(Accept());
    m.update(Down());   // Load Game
    CHECK(!m.update(Accept()) && m.screen() == MainScreen::LoadList && m.selection() == 0);
    m.update(Up());     // wraps to the last row: sr3save_11, unreadable
    CHECK(m.selection() == 11 && m.listTop() == 4 && m.saves()[11].displayName == "sr3save_11");
    CHECK(!m.update(Accept()) && m.screen() == MainScreen::LoadError);
    CHECK(!m.update(Accept()) && m.screen() == MainScreen::LoadList && m.selection() == 11 && m.listTop() == 4);
    m.update(Down());  // wraps to the top
    CHECK(m.selection() == 0 && m.listTop() == 0);
    for (int i = 0; i < 9; ++i) m.update(Down());
    CHECK(m.selection() == 9 && m.listTop() == 2);
    const auto r = m.update(Accept());  // loads at once from the main menu (Sec5.2)
    CHECK(r && r->kind == MainMenuResult::Kind::LoadSave && r->savePath == "C:/saves/sr3save_2.sr3s_pc");
    MainMenuModel e;  // nothing to load: the row is not selectable; Back returns to Campaign/Load Game
    e.update(Accept());
    e.update(Down());
    e.update(Accept());
    CHECK(!e.update(Accept()) && e.screen() == MainScreen::LoadList);
    e.update(Back());
    CHECK(e.screen() == MainScreen::Campaign && e.selection() == 1);
}

void testQuitConfirm() {
    MainMenuModel m;
    m.update(Back());  // Esc on the top list (PC): PLT_QUIT_GAME confirmation (Sec8.2)
    CHECK(m.screen() == MainScreen::ConfirmQuit && m.selection() == 1);  // starts on No
    CHECK(!m.update(Accept()) && m.screen() == MainScreen::Top && m.entries()[static_cast<size_t>(m.selection())] == MainEntry::Quit);
    m.update(Accept());
    m.update(Left());
    const auto r = m.update(Accept());
    CHECK(r && r->kind == MainMenuResult::Kind::Quit);
}

void testPause() {
    PauseMenuModel p;
    p.opened();
    CHECK(p.entries().size() == 4 && p.entries()[0] == PauseEntry::SaveGame && p.entries()[1] == PauseEntry::Options);  // no Resume entry (Sec18.1)
    CHECK(p.update(Back()) == PauseMenuAction::Resume);
    CHECK(p.update(Accept()) == PauseMenuAction::Save && !p.status().empty());
    p.setStatus("Saved: sr3save_03");
    CHECK(p.status() == "Saved: sr3save_03");
    p.update(Down());
    p.update(Down());
    CHECK(p.update(Accept()) == PauseMenuAction::None && p.screen() == PauseScreen::ConfirmQuitToMenu);
    CHECK(p.update(Accept()) == PauseMenuAction::None && p.screen() == PauseScreen::Top && p.selection() == 2);
    p.update(Accept());
    p.update(Left());
    CHECK(p.update(Accept()) == PauseMenuAction::QuitToMainMenu);
    p.opened();
    CHECK(p.selection() == 0 && p.status().empty() && p.screen() == PauseScreen::Top);
    p.update(Up());  // wraps to Quit to Desktop
    p.update(Accept());
    CHECK(p.screen() == PauseScreen::ConfirmQuitToDesktop);
    CHECK(p.update(Back()) == PauseMenuAction::None && p.screen() == PauseScreen::Top && p.selection() == 3);
    p.update(Accept());
    p.update(Right());
    CHECK(p.update(Accept()) == PauseMenuAction::QuitToDesktop);
}

void testOptions() {
    // Sec18.5 vector 4: the PC options list.
    CHECK(std::string(OptionsPageName(OptionsPage::Mouse)) == "Mouse" &&
          std::string(OptionsPageName(OptionsPage::Audio)) == "Audio");
    OptionsModel o;
    CHECK(o.value(OptionId::Sfx) == 80 && o.value(OptionId::Music) == 80 && o.value(OptionId::Voice) == 100);
    CHECK(o.value(OptionId::MouseSensV) == 50 && o.value(OptionId::VehicleCameraSnap) == 2);  // 0.24 -> 50; Smart
    CHECK(o.value(OptionId::PauseOnFocusLoss) == 1 && o.value(OptionId::Difficulty) == 1);
    // Audio page: Music 80 -> 70 (one step of 10, vector 10: channel 1 = 0.70).
    o.update(Down());
    o.update(Down());
    o.update(Down());
    o.update(Accept());
    CHECK(o.screen() == OptionsScreen::Page && o.page() == OptionsPage::Audio);
    o.update(Down());
    o.update(Left());
    CHECK(o.value(OptionId::Music) == 70 && std::fabs(o.options().audioVolume[1] - 0.70f) < 1e-6f);
    o.update(Up());
    o.update(Right());  // SFX moves channels 3, 4 and 5 together
    CHECK(o.options().audioVolume[3] == o.options().audioVolume[5] && o.value(OptionId::Sfx) == 90);
    for (int i = 0; i < 5; ++i) o.update(Right());
    CHECK(o.value(OptionId::Sfx) == 100);  // clamped
    CHECK(!o.update(Back()) && o.screen() == OptionsScreen::List && o.takeCommitted() && !o.takeCommitted());
    // Gameplay: the difficulty applies only when the page is left (Sec18.4).
    o.update(Up());
    o.update(Up());
    o.update(Accept());
    CHECK(o.page() == OptionsPage::Gameplay);
    o.update(Right());
    CHECK(o.value(OptionId::Difficulty) == 2 && o.difficulty() == 1);
    o.update(Back());
    CHECK(o.difficulty() == 2);
    // Restore Defaults (confirmed) on the Mouse page.
    o.update(Up());
    o.update(Accept());
    o.update(Right());  // Invert Y on
    CHECK(o.value(OptionId::MouseInvertY) == 1);
    for (int i = 0; i < 6; ++i) o.update(Down());  // Restore Defaults (after Remap Controls)
    o.update(Accept());
    CHECK(o.screen() == OptionsScreen::ConfirmRestore && o.selection() == 1);
    o.update(Accept());  // No
    CHECK(o.value(OptionId::MouseInvertY) == 1 && o.screen() == OptionsScreen::Page);
    o.update(Accept());
    o.update(Left());
    o.update(Accept());  // Yes
    CHECK(o.value(OptionId::MouseInvertY) == 0);
    o.update(Back());
    CHECK(o.update(Back()));  // backing out of the list closes the options

    // Profile persistence: a new image, then a round trip through the real parser.
    sr3save::ProfileOptions mod = DefaultProfileOptions();
    mod.audioVolume[1] = 0.3f;
    mod.flagsMouse = 0x07;
    mod.radio = 2;
    const std::vector<uint8_t> img = WriteProfileOptions({}, mod);
    CHECK(img.size() == sr3save::kProfileSize);
    const auto back = ReadProfileOptions(img);
    CHECK(back && back->audioVolume[1] == 0.3f && back->flagsMouse == 0x07 && back->radio == 2 &&
          back->mouseSensitivityX == 0.24f && back->formatVersion == 5);
    std::vector<uint8_t> other(sr3save::kProfileSize, 0xAB);  // an existing image keeps everything else
    other = WriteProfileOptions(other, mod);
    CHECK(other[0x10] == 0xAB && other[0x3008] == 0xAB && other[0x2000] == 0x10 && other[0x2004] == 5);
    CHECK(!ReadProfileOptions(std::vector<uint8_t>(10, 0)));

    // Options from both menus.
    MainMenuModel m;
    m.update(Down());
    m.update(Accept());
    CHECK(m.screen() == MainScreen::Options);
    m.update(Back());
    CHECK(m.screen() == MainScreen::Top && m.entries()[static_cast<size_t>(m.selection())] == MainEntry::Options);
    PauseMenuModel p;
    p.opened();
    p.update(Down());
    p.update(Accept());
    CHECK(p.screen() == PauseScreen::Options);
    CHECK(p.update(Back()) == PauseMenuAction::None && p.screen() == PauseScreen::Top && p.selection() == 1);
}

sr3tables_ui_controls::BindingSet Set(const char* name, bool common,
                                      std::vector<std::pair<const char*, const char*>> keys) {
    sr3tables_ui_controls::BindingSet s;
    s.name = name;
    s.commonBindings = common;
    for (const auto& k : keys) {
        sr3tables_ui_controls::BindingSetControl c;
        c.action = k.first;
        c.key1 = k.second;
        c.key2 = "UNBOUND";
        s.controls.push_back(c);
    }
    return s;
}

void testRemap() {
    std::vector<sr3tables_ui_controls::BindingSet> sets = {
        Set("Camera_Bindings", false, {{"CAM", "W"}}),  // never shown
        Set("General_Bindings", true, {{"PAUSE", "ESC"}, {"MAP", "TAB"}}),
        Set("On_Foot_Bindings", false, {{"RELOAD", "R"}, {"FIRE2", "Q"}, {"JUMP", "SPACEBAR"}}),
        Set("Car_Bindings", false, {{"HORN", "F"}, {"NITRO", "Q"}}),
    };
    sr3tables_ui_controls::BindingSetControl dbg;
    dbg.action = "DEBUG";
    dbg.key1 = "F1";
    dbg.debugOnly = {true, true};
    sets[2].controls.push_back(dbg);  // Debug_Only rows are not shown
    sr3tables_ui_controls::BindingSetAxis ax;
    ax.action = "MOVE";
    ax.key1 = "W";
    ax.key2 = "S";
    sets[2].axes.push_back(ax);
    BindingModel b = BindingModel::FromTable(sets);
    CHECK(b.categories().size() == 3 && b.categories()[0].label == "General" && b.categories()[1].label == "On Foot");
    CHECK(b.categories()[1].rows.size() == 5);  // 3 controls + an axis as two directions
    CHECK(b.categories()[1].rows[4].negative && b.categories()[1].rows[4].primary.key == "S");
    // On Foot Reload -> Q: Q leaves FIRE2 (same set) but not the Car set (vector 7).
    CHECK(b.bind(1, 0, false, {false, ScanCodeForKeyName("Q")}));
    CHECK(b.categories()[1].rows[1].primary.key == "UNBOUND" && b.categories()[2].rows[1].primary.key == "Q");
    CHECK(b.lastConflicts().size() == 1 && b.lastConflicts()[0].action == "FIRE2");
    // An allowed-sharing pair keeps both.
    b.allowKeySharing("JUMP", "RELOAD");
    CHECK(b.bind(1, 0, false, {false, ScanCodeForKeyName("SPACEBAR")}) && b.categories()[1].rows[2].primary.key == "SPACEBAR");
    // General is common: MAP -> Q clears Q in every set (vector 8).
    b.bind(1, 1, false, {false, ScanCodeForKeyName("Q")});
    CHECK(b.bind(0, 1, true, {false, ScanCodeForKeyName("Q")}));
    CHECK(b.categories()[1].rows[1].primary.key == "UNBOUND" && b.categories()[2].rows[1].primary.key == "UNBOUND");
    CHECK(b.categories()[0].rows[1].secondary.key == "Q" && b.categories()[0].rows[1].primary.key == "TAB");
    CHECK(!b.bind(1, 0, false, {true, 7}) && !b.bind(1, 0, false, {false, 0x100}) && !b.bind(1, 0, false, {false, 0}));
    CHECK(b.bind(1, 0, false, {true, 2}) && b.categories()[1].rows[0].primary.mouse == 2 &&
          b.categories()[1].rows[0].primary.key == "UNBOUND");
    b.resetAll();
    CHECK(b.categories()[1].rows[0].primary.key == "R" && b.categories()[2].rows[1].primary.key == "Q");

    // The Remap screens inside Options (Mouse page -> Remap Controls).
    OptionsModel o;
    o.setBindings(BindingModel::FromTable(sets));
    o.update(Accept());  // Mouse page
    for (int i = 0; i < 5; ++i) o.update(Down());
    o.update(Accept());
    CHECK(o.screen() == OptionsScreen::RemapCategories && o.selection() == 1);
    o.update(Down());  // On Foot
    o.update(Accept());
    CHECK(o.screen() == OptionsScreen::RemapRows && o.remapCategory() == 1);
    o.update(Right());  // secondary column
    o.update(Accept());
    CHECK(o.screen() == OptionsScreen::RemapCapture);
    CHECK(!o.capture({true, 9}) && o.screen() == OptionsScreen::RemapCapture);  // rejected: keeps waiting
    CHECK(o.capture({false, ScanCodeForKeyName("G")}) && o.screen() == OptionsScreen::RemapRows);
    CHECK(o.bindings()->categories()[1].rows[0].secondary.key == "G");
    o.update(Accept());
    o.update(Back());  // cancel the capture
    CHECK(o.screen() == OptionsScreen::RemapRows);
    o.update(Back());
    o.update(Up());
    o.update(Up());  // Restore Defaults row
    o.update(Accept());
    CHECK(o.screen() == OptionsScreen::ConfirmRemapReset);
    o.update(Left());
    o.update(Accept());
    CHECK(o.screen() == OptionsScreen::RemapCategories && o.bindings()->categories()[1].rows[0].secondary.key == "UNBOUND");
    o.update(Back());
    CHECK(o.screen() == OptionsScreen::Page && o.takeCommitted());
    OptionsModel none;  // no binding table: the item does nothing
    none.update(Accept());
    for (int i = 0; i < 5; ++i) none.update(Down());
    none.update(Accept());
    CHECK(none.screen() == OptionsScreen::Page);
}

void testGamepad() {
    GamepadReader g;
    PadState s;
    CHECK(!g.step(s).connected);
    s.connected = true;
    s.a = true;
    GamepadEdges e = g.step(s);
    CHECK(e.connected && e.menu.accept);
    CHECK(!g.step(s).menu.accept);  // held: no new edge
    s.a = false;
    s.stickY = 0.6f;
    CHECK(g.step(s).menu.up);
    s.stickY = 0.4f;  // between release and press: still held
    CHECK(!g.step(s).menu.up);
    s.stickY = 0.2f;
    g.step(s);
    s.stickY = 0.6f;
    CHECK(g.step(s).menu.up);
    s.stickY = 0.0f;
    s.down = true;
    s.start = true;
    e = g.step(s);
    CHECK(e.menu.down && e.start && !e.menu.up);
    const FrontendInput m = Merge(Up(), e.menu);
    CHECK(m.up && m.down && !m.accept);
}

}  // namespace

int main() {
    testContinue();
    testNewGame();
    testLoadList();
    testQuitConfirm();
    testPause();
    testGamepad();
    testOptions();
    testRemap();
    if (g_failures != 0) {
        std::cerr << g_failures << " of " << g_checks << " checks FAILED.\n";
        return 1;
    }
    std::cout << g_checks << " checks passed.\n";
    return 0;
}
