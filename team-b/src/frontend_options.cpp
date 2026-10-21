// Options menu and profile persistence. See include/sr3frontend/options.h.

#include "sr3frontend/options.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace sr3frontend {

namespace {

constexpr double kMouseSensUiScale = 0.48;  // CHOSEN: 0.24 (shipped) shows as 50
constexpr double kBrightnessUiScale = 2.0;  // CHOSEN: UI 0..100 = 0.0..2.0

void Put32(std::vector<uint8_t>& b, size_t o, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[o + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
}
void PutF(std::vector<uint8_t>& b, size_t o, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    Put32(b, o, v);
}
int Bit(uint8_t flags, int bit) { return (flags >> bit) & 1; }
void SetBit(uint8_t& flags, int bit, int on) {
    flags = static_cast<uint8_t>(on ? (flags | (1u << bit)) : (flags & ~(1u << bit)));
}
int Ui(double v, double scale) { return static_cast<int>(std::lround(std::clamp(v / scale * 100.0, 0.0, 100.0))); }
float FromUi(int ui, double scale) { return static_cast<float>(ui / 100.0 * scale); }

}  // namespace

sr3save::ProfileOptions DefaultProfileOptions() {
    sr3save::ProfileOptions o;
    o.formatVersion = sr3save::kProfileOptionsVersion;
    o.flagsA = 0x18;  // vibration + crouch toggle on
    o.item08Open = 3;
    o.lookSensitivityH = o.lookSensitivityV = 1.0f;
    o.gamma = 0.5f;
    o.brightness = 1.0f;
    o.audioVolume = {0.8f, 0.8f, 1.0f, 0.8f, 0.8f, 0.8f, 1.0f};  // ch1 music 80, ch2 voice 100, ch3-5 SFX 80
    o.radio = 0;
    o.audioItem7Open = 1;
    o.flagsMouse = 0x01;  // pause on focus loss
    o.mouseSensitivityX = o.mouseSensitivityY = 0.24f;
    o.mouseItem5Open = 2;  // vehicle camera snap (mouse): Smart
    o.item5COpen = 1;
    o.cutsceneVsyncByte = 0x13;
    return o;
}

std::vector<uint8_t> WriteProfileOptions(std::vector<uint8_t> b, const sr3save::ProfileOptions& o) {
    if (b.size() != sr3save::kProfileSize) {
        b.assign(sr3save::kProfileSize, 0);
        Put32(b, sr3save::kProfileHeaderOffset + 0x08, static_cast<uint32_t>(sr3save::kProfileSettingsSize));
        b[sr3save::kProfileHeaderOffset + 0x19] = 1;  // dirty: always 1 in a saved file
    }
    const size_t s = sr3save::kProfileSettingsOffset;
    Put32(b, s, sr3save::kProfileSettingsMagic);
    const size_t p = s + 4;
    Put32(b, p + 0x00, o.formatVersion);
    b[p + 0x04] = o.flagsA;
    Put32(b, p + 0x08, static_cast<uint32_t>(o.item08Open));
    PutF(b, p + 0x0C, o.lookSensitivityH);
    PutF(b, p + 0x10, o.lookSensitivityV);
    Put32(b, p + 0x14, static_cast<uint32_t>(o.controlPresetList1));
    Put32(b, p + 0x18, static_cast<uint32_t>(o.controlPresetList2));
    PutF(b, p + 0x1C, o.gamma);
    b[p + 0x20] = o.subtitles;
    b[p + 0x21] = o.staticMinimap;
    b[p + 0x22] = o.gameplayVsync;
    b[p + 0x23] = o.cutsceneVsyncByte;
    PutF(b, p + 0x24, o.brightness);
    for (size_t i = 0; i < 7; ++i) PutF(b, p + 0x28 + 4 * i, o.audioVolume[i]);
    Put32(b, p + 0x44, static_cast<uint32_t>(o.radio));
    Put32(b, p + 0x48, static_cast<uint32_t>(o.audioItem7Open));
    b[p + 0x4C] = o.flagsMouse;
    PutF(b, p + 0x50, o.mouseSensitivityX);
    PutF(b, p + 0x54, o.mouseSensitivityY);
    Put32(b, p + 0x58, static_cast<uint32_t>(o.mouseItem5Open));
    Put32(b, p + 0x5C, static_cast<uint32_t>(o.item5COpen));
    b[p + 0x60] = o.flagsVehicle;
    return b;
}

std::optional<sr3save::ProfileOptions> ReadProfileOptions(const std::vector<uint8_t>& image) {
    try {
        const sr3save::SaveProfile p = sr3save::SaveProfile::parse(vpp::ByteView(image.data(), image.size()));
        if (!p.settingsMagicOk() || !p.optionsApplied()) return std::nullopt;
        return p.options();
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string ProfilePath(const std::string& dir) { return dir + "/sr3def_profile"; }

sr3save::ProfileOptions LoadOptions(const std::string& dir) {
    std::ifstream in(ProfilePath(dir), std::ios::binary);
    if (!in) return DefaultProfileOptions();
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return ReadProfileOptions(bytes).value_or(DefaultProfileOptions());
}

bool SaveOptions(const std::string& dir, const sr3save::ProfileOptions& o) {
    std::vector<uint8_t> bytes;
    {
        std::ifstream in(ProfilePath(dir), std::ios::binary);
        if (in) bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    bytes = WriteProfileOptions(std::move(bytes), o);
    std::ofstream out(ProfilePath(dir), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

const char* OptionsPageName(OptionsPage page) {
    switch (page) {
    case OptionsPage::Mouse: return "Mouse";
    case OptionsPage::Gameplay: return "Gameplay";
    case OptionsPage::Display: return "Display";
    case OptionsPage::Audio: return "Audio";
    }
    return "";
}

std::vector<OptionItem> OptionItems(OptionsPage page) {
    const std::vector<std::string> noYes = {"No", "Yes"};
    switch (page) {
    case OptionsPage::Mouse:
        return {{OptionId::MouseInvertY, "Invert Y Axis", OptionKind::Toggle, noYes},
                {OptionId::MouseInvertX, "Invert X Axis", OptionKind::Toggle, noYes},
                {OptionId::MouseSensV, "Vertical Sensitivity", OptionKind::Slider, {}, 1},
                {OptionId::MouseSensH, "Horizontal Sensitivity", OptionKind::Slider, {}, 1},
                {OptionId::VehicleCameraSnap, "Vehicle Camera Snap", OptionKind::Choice, {"Instant", "Delayed", "Smart"}},
                {OptionId::RemapControls, "Remap Controls", OptionKind::Action, {}},
                {OptionId::RestoreDefaults, "Restore Defaults", OptionKind::Action, {}}};
    case OptionsPage::Gameplay:
        return {{OptionId::Difficulty, "Difficulty", OptionKind::Choice, {"Casual", "Normal", "Hardcore"}},
                {OptionId::AirplaneControls, "Airplane Controls", OptionKind::Choice, {"Advanced", "Normal"}},
                {OptionId::Subtitles, "Subtitles", OptionKind::Toggle, noYes},
                {OptionId::Radio, "Radio", OptionKind::Choice, {"Always On", "Hijack", "Life-like"}},
                {OptionId::MinimapView, "Minimap View", OptionKind::Choice, {"Rotational", "Static"}},
                {OptionId::PauseOnFocusLoss, "Pause on Focus Loss", OptionKind::Toggle, noYes},
                {OptionId::RestoreDefaults, "Restore Defaults", OptionKind::Action, {}}};
    case OptionsPage::Display:
        return {{OptionId::Brightness, "Brightness", OptionKind::Slider, {}, 1},
                {OptionId::GameplayVsync, "Gameplay VSync", OptionKind::Toggle, noYes},
                {OptionId::MinimapView, "Minimap View", OptionKind::Choice, {"Rotational", "Static"}}};
    case OptionsPage::Audio:
        return {{OptionId::Sfx, "Sound Effects Volume", OptionKind::Slider, {}, 10},
                {OptionId::Music, "Music Volume", OptionKind::Slider, {}, 10},
                {OptionId::Voice, "Voice Volume", OptionKind::Slider, {}, 10},
                {OptionId::Radio, "Radio", OptionKind::Choice, {"Always On", "Hijack", "Life-like"}},
                {OptionId::Subtitles, "Subtitles", OptionKind::Toggle, noYes},
                {OptionId::RestoreDefaults, "Restore Defaults", OptionKind::Action, {}}};
    }
    return {};
}

OptionsModel::OptionsModel(sr3save::ProfileOptions o, int difficulty)
    : o_(o), difficulty_(difficulty), pendingDifficulty_(difficulty) {}

void OptionsModel::opened() {
    screen_ = OptionsScreen::List;
    sel_ = 0;
}

int OptionsModel::value(OptionId id) const {
    switch (id) {
    case OptionId::MouseInvertY: return Bit(o_.flagsMouse, 1);
    case OptionId::MouseInvertX: return Bit(o_.flagsMouse, 2);
    case OptionId::MouseSensV: return Ui(o_.mouseSensitivityY, kMouseSensUiScale);
    case OptionId::MouseSensH: return Ui(o_.mouseSensitivityX, kMouseSensUiScale);
    case OptionId::VehicleCameraSnap: return std::clamp(o_.mouseItem5Open, 0, 2);
    case OptionId::Difficulty: return pendingDifficulty_;
    case OptionId::AirplaneControls: return Bit(o_.flagsVehicle, 1);
    case OptionId::Subtitles: return o_.subtitles ? 1 : 0;
    case OptionId::Radio: return std::clamp(o_.radio, 0, 2);
    case OptionId::MinimapView: return o_.staticMinimap ? 1 : 0;
    case OptionId::PauseOnFocusLoss: return Bit(o_.flagsMouse, 0);
    case OptionId::Brightness: return Ui(o_.brightness, kBrightnessUiScale);
    case OptionId::GameplayVsync: return o_.gameplayVsync ? 1 : 0;
    case OptionId::Sfx: return Ui(o_.audioVolume[3], 1.0);
    case OptionId::Music: return Ui(o_.audioVolume[1], 1.0);
    case OptionId::Voice: return Ui(o_.audioVolume[2], 1.0);
    case OptionId::RestoreDefaults:
    case OptionId::RemapControls: return 0;
    }
    return 0;
}

void OptionsModel::setValue(OptionId id, int v) {
    const int b = v ? 1 : 0;
    const int c3 = std::clamp(v, 0, 2);
    const int s = std::clamp(v, 0, 100);
    switch (id) {
    case OptionId::MouseInvertY: SetBit(o_.flagsMouse, 1, b); break;
    case OptionId::MouseInvertX: SetBit(o_.flagsMouse, 2, b); break;
    case OptionId::MouseSensV: o_.mouseSensitivityY = FromUi(s, kMouseSensUiScale); break;
    case OptionId::MouseSensH: o_.mouseSensitivityX = FromUi(s, kMouseSensUiScale); break;
    case OptionId::VehicleCameraSnap: o_.mouseItem5Open = c3; break;
    case OptionId::Difficulty: pendingDifficulty_ = c3; break;  // applied when the page is left
    case OptionId::AirplaneControls: SetBit(o_.flagsVehicle, 1, b); break;
    case OptionId::Subtitles: o_.subtitles = static_cast<uint8_t>(b); break;
    case OptionId::Radio: o_.radio = c3; break;
    case OptionId::MinimapView: o_.staticMinimap = static_cast<uint8_t>(b); break;
    case OptionId::PauseOnFocusLoss: SetBit(o_.flagsMouse, 0, b); break;
    case OptionId::Brightness: o_.brightness = FromUi(s, kBrightnessUiScale); break;
    case OptionId::GameplayVsync: o_.gameplayVsync = static_cast<uint8_t>(b); break;
    case OptionId::Sfx: o_.audioVolume[3] = o_.audioVolume[4] = o_.audioVolume[5] = FromUi(s, 1.0); break;  // one control
    case OptionId::Music: o_.audioVolume[1] = FromUi(s, 1.0); break;
    case OptionId::Voice: o_.audioVolume[2] = FromUi(s, 1.0); break;
    case OptionId::RestoreDefaults:
    case OptionId::RemapControls: break;
    }
}

std::string OptionsModel::valueText(const OptionItem& item) const {
    const int v = value(item.id);
    switch (item.kind) {
    case OptionKind::Toggle:
    case OptionKind::Choice: return item.choices[static_cast<size_t>(std::clamp(v, 0, static_cast<int>(item.choices.size()) - 1))];
    case OptionKind::Slider: return std::to_string(v);
    case OptionKind::Action: return std::string();
    }
    return std::string();
}

void OptionsModel::restoreDefaults(OptionsPage page) {
    const OptionsModel defaults(DefaultProfileOptions(), 1);
    for (const OptionItem& it : OptionItems(page))
        if (it.kind != OptionKind::Action) setValue(it.id, defaults.value(it.id));
}

bool OptionsModel::capture(const BindInput& in) {
    if (screen_ != OptionsScreen::RemapCapture || !bindings_) return false;
    if (!bindings_->bind(remapCat_, remapRow_, remapCol_ == 1, in)) return false;  // rejected: keep waiting
    screen_ = OptionsScreen::RemapRows;
    return true;
}

bool OptionsModel::takeCommitted() {
    const bool c = committed_;
    committed_ = false;
    return c;
}

bool OptionsModel::update(const sr3game::FrontendInput& in) {
    constexpr int kPages = 4;
    switch (screen_) {
    case OptionsScreen::List:
        if (in.down) sel_ = (sel_ + 1) % kPages;
        if (in.up) sel_ = (sel_ + kPages - 1) % kPages;
        if (in.back) return true;
        if (in.accept) {
            page_ = static_cast<OptionsPage>(sel_);
            screen_ = OptionsScreen::Page;
            pageSel_ = 0;
        }
        return false;
    case OptionsScreen::Page: {
        const std::vector<OptionItem> items = OptionItems(page_);
        const int n = static_cast<int>(items.size());
        if (in.down) pageSel_ = (pageSel_ + 1) % n;
        if (in.up) pageSel_ = (pageSel_ + n - 1) % n;
        const OptionItem& it = items[static_cast<size_t>(pageSel_)];
        if (in.left || in.right) {
            const int d = in.right ? 1 : -1;
            const int v = value(it.id);
            if (it.kind == OptionKind::Slider) setValue(it.id, std::clamp(v + d * it.step, 0, 100));
            else if (it.kind != OptionKind::Action) {
                const int count = static_cast<int>(it.choices.size());
                setValue(it.id, (v + d + count) % count);
            }
        }
        if (in.accept && it.id == OptionId::RemapControls) {
            if (bindings_) {
                screen_ = OptionsScreen::RemapCategories;
                sel_ = 1;  // General
            }
            return false;
        }
        if (in.accept && it.kind == OptionKind::Action) {
            screen_ = OptionsScreen::ConfirmRestore;
            sel_ = 1;  // No
            return false;
        }
        if (in.back) {  // pause_menu_accept_options; the difficulty is applied now (values > 2 ignored)
            if (page_ == OptionsPage::Gameplay && pendingDifficulty_ <= 2) difficulty_ = pendingDifficulty_;
            committed_ = true;
            screen_ = OptionsScreen::List;
            sel_ = static_cast<int>(page_);
        }
        return false;
    }
    case OptionsScreen::RemapCategories: {
        const int n = static_cast<int>(bindings_->categories().size()) + 1;  // row 0 = Restore Defaults
        if (in.down) sel_ = (sel_ + 1) % n;
        if (in.up) sel_ = (sel_ + n - 1) % n;
        if (in.back) {  // Back commits (pause_menu_accept_options)
            committed_ = true;
            screen_ = OptionsScreen::Page;
            return false;
        }
        if (in.accept) {
            if (sel_ == 0) {
                screen_ = OptionsScreen::ConfirmRemapReset;
                sel_ = 1;
            } else {
                remapCat_ = sel_ - 1;
                remapRow_ = remapCol_ = 0;
                screen_ = OptionsScreen::RemapRows;
            }
        }
        return false;
    }
    case OptionsScreen::RemapRows: {
        const int n = static_cast<int>(bindings_->categories()[static_cast<size_t>(remapCat_)].rows.size());
        if (n > 0) {
            if (in.down) remapRow_ = (remapRow_ + 1) % n;
            if (in.up) remapRow_ = (remapRow_ + n - 1) % n;
        }
        if (in.left || in.right) remapCol_ = 1 - remapCol_;
        if (in.back) {
            screen_ = OptionsScreen::RemapCategories;
            sel_ = remapCat_ + 1;
        } else if (in.accept && n > 0) {
            screen_ = OptionsScreen::RemapCapture;
        }
        return false;
    }
    case OptionsScreen::RemapCapture:
        if (in.back) screen_ = OptionsScreen::RemapRows;  // the capture itself comes through capture()
        return false;
    case OptionsScreen::ConfirmRemapReset:
        if (in.left || in.right || in.up || in.down) sel_ = 1 - sel_;
        if (in.accept && sel_ == 0) bindings_->resetAll();
        if (in.accept || in.back) {
            screen_ = OptionsScreen::RemapCategories;
            sel_ = 0;
        }
        return false;
    case OptionsScreen::ConfirmRestore:
        if (in.left || in.right || in.up || in.down) sel_ = 1 - sel_;
        if (in.accept && sel_ == 0) restoreDefaults(page_);
        if (in.accept || in.back) screen_ = OptionsScreen::Page;
        return false;
    }
    return false;
}

}  // namespace sr3frontend
