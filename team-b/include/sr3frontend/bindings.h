// PC control rebinding for sr3.exe (Team C, HANDOFF-C.md section 10) - spec-tables-ui-controls.md
// Sec18.3 over the real control_binding_sets.xtbl (Team B's ParseControlBindingSetsTable).
//   * Six categories in this order: General, On Foot, Car, Helicopter, Plane, Tank (the Camera set
//     is never shown). Debug_Only / Non_Release_Final rows are not bindable. Each row has a primary
//     and a secondary column; an axis row covers one direction (positive, then negative).
//   * Accepted inputs: a keyboard scan code < 0x100 (not 0), or a mouse button 0..6 (vector 9).
//   * Conflict rule: before the write, every OTHER row of the same set holding the same key / mouse
//     button loses it (cleared to UNBOUND), and is recorded; if the target set has Common_Bindings
//     (General), the sweep covers every set. No refusal, no swap. Allowed-sharing pairs are skipped.
//   * Reset restores every set to the shipped table.
// CHOSEN (HANDOFF-C 10): a cell holds one input - writing a key clears that cell's mouse button and
// the other way round; the allowed-sharing pair lists are empty by default (their contents were not
// dumped - callers can add pairs); key names for scan codes the spec does not list come from the
// standard set-1 scan codes (KeyNameForScanCode).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sr3tables_ui_controls/tables.h"

namespace sr3frontend {

// Key name <-> DirectInput scan code (spec-tables-ui-controls Sec4.3: UNBOUND 0, ESC 0x01, digits
// 0x02-0x0B, TAB 0x0F, ENTER 0x1C, LEFT CTRL 0x1D, A 0x1E, Z 0x2C, SPACEBAR 0x39, F1-F10 0x3B-0x44,
// F11 0x57, F12 0x58, RIGHT CTRL 0x9D, arrows 0xC8/0xD0/0xCB/0xCD; the rest standard set 1).
std::string KeyNameForScanCode(int code);  // "" when unknown
int ScanCodeForKeyName(const std::string& name);  // 0 when unknown / UNBOUND
// Mouse button names (Sec4.4): MOUSE LEFT 0 ... WHEEL DOWN 6.
std::string MouseButtonName(int button);

struct BindSlot {
    std::string key = "UNBOUND";
    int mouse = -1;
    bool empty() const { return (key.empty() || key == "UNBOUND") && mouse < 0; }
};
struct BindRow {
    std::string action;
    bool axis = false;
    bool negative = false;  // the axis direction this row covers
    BindSlot primary, secondary;
};
struct BindCategory {
    std::string setName;  // e.g. On_Foot_Bindings
    std::string label;    // e.g. On Foot
    bool common = false;  // Common_Bindings (General)
    std::vector<BindRow> rows;
};

// One captured input from the "press any key" dialog.
struct BindInput {
    bool mouse = false;
    int code = 0;  // scan code, or mouse button
};
bool BindInputAccepted(const BindInput& in);  // key 1..0xFF with a name, or mouse 0..6

struct BindConflict {
    std::string setName, action;
    bool secondary = false;
};

class BindingModel {
public:
    static BindingModel FromTable(const std::vector<sr3tables_ui_controls::BindingSet>& sets);
    const std::vector<BindCategory>& categories() const { return cats_; }
    // vint_options_remap_set_key_binding + the commit: false (nothing written) for a rejected input.
    bool bind(int category, int row, bool secondary, const BindInput& in);
    const std::vector<BindConflict>& lastConflicts() const { return conflicts_; }
    void resetAll() { cats_ = defaults_; }
    // Allowed-sharing pairs (actions that may hold the same key / mouse button).
    void allowKeySharing(const std::string& a, const std::string& b) { keyPairs_.push_back({a, b}); }
    void allowMouseSharing(const std::string& a, const std::string& b) { mousePairs_.push_back({a, b}); }

private:
    bool shared(const std::vector<std::pair<std::string, std::string>>& pairs, const std::string& a,
                const std::string& b) const;
    std::vector<BindCategory> cats_, defaults_;
    std::vector<BindConflict> conflicts_;
    std::vector<std::pair<std::string, std::string>> keyPairs_, mousePairs_;
};

}  // namespace sr3frontend
