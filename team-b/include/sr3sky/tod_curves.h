// Base time-of-day curves - spec-tables-environment.md 19.1: default_district.xtbl holds one element per
// lighting value (east0..east3, west0..west3, zenith, fog_color, ambient_color, back_ambient_color,
// tod_light_color, fog_* floats, ...), each with <key><time>slot</time><value>...</value></key> keys on
// half-hour slots 0..47. The keys are resampled to all 48 slots (0x00BA36E0, with the shipped "47 span"
// quirk on the wrap) and read per frame by blending slot floor(2H) with the next one (0x00BA2E30).
// Colours are "r g b intensity"; the HDR pack (0x00BA5F80, shipped on) is rgb x intensity.
#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3sky {

using Value4 = std::array<float, 4>;  // up to 4 components; floats use [0]

struct TodCurve {
    int components = 0;  // 1 for a float curve, 4 for a colour
    std::array<Value4, 48> slots{};
};

// 0x00BA36E0: fills the 48 slots from the keys (slot -> value). No key at all: `defaultValue`.
std::array<Value4, 48> ResampleTodKeys(const std::map<int, Value4>& keys, const Value4& defaultValue = {});
// 0x00BA2E30: x = 2H, slot = floor(x) mod 48, frac = x - floor(x); blend slot with slot + 1 (mod 48).
Value4 SampleTodCurve(const TodCurve& c, float hour);

class TodBaseCurves {
public:
    // default_district.xtbl (root element children are the curves).
    static TodBaseCurves Parse(const sr3xtbl::Document& doc);
    bool empty() const { return curves_.empty(); }
    bool has(const std::string& name) const;
    std::optional<Value4> sample(const std::string& name, float hour) const;
    // A colour curve as rgb x intensity (the HDR pack), if present.
    std::optional<std::array<float, 3>> colour(const std::string& name, float hour) const;
    size_t size() const { return curves_.size(); }

private:
    std::map<std::string, TodCurve> curves_;  // keyed by lower-case element name
};

// The weather merge over the base (spec-tables-environment 19.1 "Live wiring", D-24, FINAL): per field,
// F = the from-cell value if present else base, T = the to-cell value if present else base,
// out = f32(f32(F x f32(1 - t)) + f32(T x t)) on all 4 channels; neither present, or the merge switched off
// (script-forced weather), gives exactly the base. `from`/`to` null = the cell does not mark the field.
Value4 MergeWeatherField(const Value4& base, const Value4* from, const Value4* to, float t, bool mergeOn = true);

}  // namespace sr3sky
