// PC control rebinding. See include/sr3frontend/bindings.h.

#include "sr3frontend/bindings.h"

#include <algorithm>
#include <map>

namespace sr3frontend {

namespace {

const std::vector<std::pair<int, const char*>>& KeyTable() {
    static const std::vector<std::pair<int, const char*>> k = {
        {0x01, "ESC"}, {0x02, "1"}, {0x03, "2"}, {0x04, "3"}, {0x05, "4"}, {0x06, "5"}, {0x07, "6"}, {0x08, "7"},
        {0x09, "8"}, {0x0A, "9"}, {0x0B, "0"}, {0x0C, "MINUS"}, {0x0D, "EQUALS"}, {0x0E, "BACKSPACE"}, {0x0F, "TAB"},
        {0x10, "Q"}, {0x11, "W"}, {0x12, "E"}, {0x13, "R"}, {0x14, "T"}, {0x15, "Y"}, {0x16, "U"}, {0x17, "I"},
        {0x18, "O"}, {0x19, "P"}, {0x1A, "LEFT BRACKET"}, {0x1B, "RIGHT BRACKET"}, {0x1C, "ENTER"}, {0x1D, "LEFT CTRL"},
        {0x1E, "A"}, {0x1F, "S"}, {0x20, "D"}, {0x21, "F"}, {0x22, "G"}, {0x23, "H"}, {0x24, "J"}, {0x25, "K"},
        {0x26, "L"}, {0x27, "SEMICOLON"}, {0x28, "APOSTROPHE"}, {0x29, "GRAVE"}, {0x2A, "LEFT SHIFT"},
        {0x2B, "BACKSLASH"}, {0x2C, "Z"}, {0x2D, "X"}, {0x2E, "C"}, {0x2F, "V"}, {0x30, "B"}, {0x31, "N"},
        {0x32, "M"}, {0x33, "COMMA"}, {0x34, "PERIOD"}, {0x35, "SLASH"}, {0x36, "RIGHT SHIFT"}, {0x38, "LEFT ALT"},
        {0x39, "SPACEBAR"}, {0x3A, "CAPS LOCK"}, {0x3B, "F1"}, {0x3C, "F2"}, {0x3D, "F3"}, {0x3E, "F4"},
        {0x3F, "F5"}, {0x40, "F6"}, {0x41, "F7"}, {0x42, "F8"}, {0x43, "F9"}, {0x44, "F10"}, {0x57, "F11"},
        {0x58, "F12"}, {0x9D, "RIGHT CTRL"}, {0xB8, "RIGHT ALT"}, {0xC8, "UP ARROW"}, {0xD0, "DOWN ARROW"},
        {0xCB, "LEFT ARROW"}, {0xCD, "RIGHT ARROW"}, {0xC7, "HOME"}, {0xCF, "END"}, {0xC9, "PAGE UP"},
        {0xD1, "PAGE DOWN"}, {0xD2, "INSERT"}, {0xD3, "DELETE"},
    };
    return k;
}

struct SetOrder {
    const char* set;
    const char* label;
};
constexpr SetOrder kOrder[] = {{"General_Bindings", "General"}, {"On_Foot_Bindings", "On Foot"},
                               {"Car_Bindings", "Car"},         {"Helicopter_Bindings", "Helicopter"},
                               {"Airplane_Bindings", "Plane"},  {"Tank_Bindings", "Tank"}};

BindSlot Slot(const std::optional<std::string>& key, int mouse) {
    BindSlot s;
    s.key = key.value_or("UNBOUND");
    s.mouse = mouse;
    return s;
}

}  // namespace

std::string KeyNameForScanCode(int code) {
    for (const auto& k : KeyTable())
        if (k.first == code) return k.second;
    return std::string();
}

int ScanCodeForKeyName(const std::string& name) {
    for (const auto& k : KeyTable())
        if (sr3xtbl::NameEquals(k.second, name)) return k.first;
    return 0;
}

std::string MouseButtonName(int b) {
    static const char* kNames[] = {"MOUSE LEFT", "MOUSE MIDDLE", "MOUSE RIGHT", "MOUSE 4", "MOUSE 5", "WHEEL UP", "WHEEL DOWN"};
    return b >= 0 && b < 7 ? kNames[b] : "UNBOUND";
}

bool BindInputAccepted(const BindInput& in) {
    if (in.mouse) return in.code >= 0 && in.code <= 6;
    return in.code > 0 && in.code < 0x100 && !KeyNameForScanCode(in.code).empty();
}

BindingModel BindingModel::FromTable(const std::vector<sr3tables_ui_controls::BindingSet>& sets) {
    BindingModel m;
    for (const SetOrder& o : kOrder) {
        for (const auto& s : sets) {
            if (!s.name || !sr3xtbl::NameEquals(*s.name, o.set)) continue;
            BindCategory c;
            c.setName = *s.name;
            c.label = o.label;
            c.common = s.commonBindings.value_or(false);
            for (const auto& ctl : s.controls) {
                if (ctl.debugOnly.value || ctl.nonReleaseFinal.value || !ctl.action) continue;
                c.rows.push_back({*ctl.action, false, false, Slot(ctl.key1, ctl.mouse1), Slot(ctl.key2, ctl.mouse2)});
            }
            for (const auto& ax : s.axes) {
                if (!ax.action) continue;
                c.rows.push_back({*ax.action, true, false, Slot(ax.key1, ax.mouse1), Slot(ax.key3, ax.mouse3)});
                c.rows.push_back({*ax.action, true, true, Slot(ax.key2, ax.mouse2), Slot(ax.key4, ax.mouse4)});
            }
            m.cats_.push_back(std::move(c));
            break;
        }
    }
    m.defaults_ = m.cats_;
    return m;
}

bool BindingModel::shared(const std::vector<std::pair<std::string, std::string>>& pairs, const std::string& a,
                          const std::string& b) const {
    for (const auto& p : pairs)
        if ((p.first == a && p.second == b) || (p.first == b && p.second == a)) return true;
    return false;
}

bool BindingModel::bind(int category, int row, bool secondary, const BindInput& in) {
    conflicts_.clear();
    if (!BindInputAccepted(in)) return false;  // no write
    if (category < 0 || category >= static_cast<int>(cats_.size())) return false;
    BindCategory& target = cats_[static_cast<size_t>(category)];
    if (row < 0 || row >= static_cast<int>(target.rows.size())) return false;
    BindRow& tr = target.rows[static_cast<size_t>(row)];
    const std::string keyName = in.mouse ? std::string() : KeyNameForScanCode(in.code);
    // The conflict sweep: the target set, or every set when it is the common one.
    for (size_t ci = 0; ci < cats_.size(); ++ci) {
        if (!target.common && static_cast<int>(ci) != category) continue;
        BindCategory& c = cats_[ci];
        for (size_t ri = 0; ri < c.rows.size(); ++ri) {
            BindRow& r = c.rows[ri];
            if (static_cast<int>(ci) == category && static_cast<int>(ri) == row) continue;
            if (in.mouse ? shared(mousePairs_, r.action, tr.action) : shared(keyPairs_, r.action, tr.action)) continue;
            for (int col = 0; col < 2; ++col) {
                BindSlot& s = col ? r.secondary : r.primary;
                const bool hit = in.mouse ? s.mouse == in.code : sr3xtbl::NameEquals(s.key, keyName);
                if (!hit) continue;
                if (in.mouse) s.mouse = -1;
                else s.key = "UNBOUND";
                conflicts_.push_back({c.setName, r.action, col == 1});
            }
        }
    }
    BindSlot& dst = secondary ? tr.secondary : tr.primary;
    if (in.mouse) {
        dst.mouse = in.code;
        dst.key = "UNBOUND";
    } else {
        dst.key = keyName;
        dst.mouse = -1;
    }
    return true;
}

}  // namespace sr3frontend
