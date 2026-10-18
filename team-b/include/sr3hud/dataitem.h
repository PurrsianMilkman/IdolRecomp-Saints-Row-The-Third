// HUD dataitems - spec-hud-runtime.md "Free-roam HUD binding map", H.1.
//
// The engine creates every core HUD dataitem once, with a fixed per-field
// type list, and producers write fields one at a time. A field write
// compares the new value with the stored one and returns at once when they
// are equal; otherwise it stores the value and marks the item dirty. So a
// producer may push every frame while the Lua "update" callback only sees
// items that actually changed (H.1: CONFIRMED for the compare and the dirty
// byte; that the callback dispatch reads the dirty byte is HIGH CONFIDENCE).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace sr3hud {

// The VINT tagged-value codes the field-type lists use (H.1).
enum class FieldType : uint8_t {
    Int = 1,
    Uint = 2,
    Float = 3,
    String = 4,
    Bool = 5,
};

using FieldValue = std::variant<int32_t, uint32_t, float, std::string, bool>;

FieldType TypeOf(const FieldValue& v);
FieldValue DefaultValue(FieldType t);

struct FieldDesc {
    std::string name;
    FieldType type;
};

class DataItem {
public:
    DataItem() = default;
    DataItem(std::string name, std::vector<FieldDesc> fields);

    const std::string& name() const { return name_; }
    size_t fieldCount() const { return fields_.size(); }
    const FieldDesc& field(size_t i) const { return fields_[i]; }
    // Index of the named field, or -1.
    int fieldIndex(const std::string& fieldName) const;

    // Set field i. Returns true when the stored value changed (and the item
    // was marked dirty). An out-of-range index or a value whose type does
    // not match the field's declared type is refused (returns false,
    // nothing stored) - a typed dataitem never silently converts.
    bool set(size_t i, const FieldValue& v);

    const FieldValue& get(size_t i) const { return values_[i]; }
    int32_t getInt(size_t i) const { return std::get<int32_t>(values_[i]); }
    uint32_t getUint(size_t i) const { return std::get<uint32_t>(values_[i]); }
    float getFloat(size_t i) const { return std::get<float>(values_[i]); }
    const std::string& getString(size_t i) const { return std::get<std::string>(values_[i]); }
    bool getBool(size_t i) const { return std::get<bool>(values_[i]); }

    bool dirty() const { return dirty_; }
    // Read and clear the dirty mark (what an "update" dispatch would do).
    bool consumeDirty();

private:
    std::string name_;
    std::vector<FieldDesc> fields_;
    std::vector<FieldValue> values_;
    bool dirty_ = false;
};

// ---------------------------------------------------------------------------
// The core HUD dataitems of H.1, field indices as the spec lists them.
// ---------------------------------------------------------------------------

namespace status {  // "sr2_local_player_status", 13 fields, pushed every frame
enum : size_t {
    SprintEnabled = 0, HealthPct, HealthLevel, IsDbno, BleedOutHealthPct,
    SprintPct, SprintLevel, IsSprinting, Orientation, IsMpInvulnerable,
    TunnelVisionAmount, IsInSatelliteMode, IsControllingMissile, Count
};
}

namespace infrequent {  // "sr2_local_player_status_infrequent", 9 fields
enum : size_t {
    Cash = 0, MaxFollowers, InventoryDisabled, VehicleLogo, RadioStation,
    CruiseControlActive, CameraScreenshotEnabled, ForceShowCash, ShowVtolHints,
    Count
};
}

namespace respect {  // "sr2_local_player_respect", 5 fields
enum : size_t { TotalRespect = 0, RespectPct, RespectLevel, ShowUpgrades, ForceShowRespect, Count };
}

// "sr2_local_player_weapons" has 25 fields. The spec names fields 0-4 and
// 6-11 (H.2 ammo / weapon-icon rows); the rest are listed only in a comment
// inside the shipped hud.lua, which this tree does not carry. Those fields
// exist here as "field_N" placeholders so the item has the right width.
namespace weapons {
enum : size_t {
    WpnBmpName = 0, RdyAmmo = 1, RsvAmmo = 2, GrenadeBmpName = 3, GrenadeAmmo = 4,
    DualWield = 6, NoAmmo = 7, InfAmmo = 8, GndInfAmmo = 9, ContFire = 10, NoMag = 11,
    Count = 25
};
}

namespace frequent_weapon {  // "sr2_local_player_frequent_weapon", 8 fields
enum : size_t {
    AmmoReady = 0, ReticleHighlight, CurWpnSpread, FineAimTransition,
    WaterPressure, XScreenCoord, YScreenCoord, ReticuleOpacity, Count
};
}

namespace notoriety {  // each item of the "sr2_notoriety" datagroup
enum : size_t { TeamName = 0, NotoLevel, NotoIcon, Count };
constexpr size_t kItemCount = 4;
}

namespace hood {  // "sr2_local_player_hood"
enum : size_t { HoodName = 0, CurHoodOwner, OrigHoodOwner, IsContested, Count };
}

// Every core dataitem, created with the H.1 names and field-type lists.
struct HudDataItems {
    HudDataItems();

    DataItem status;
    DataItem infrequent;
    DataItem respect;
    DataItem weapons;
    DataItem frequentWeapon;
    DataItem notoriety[notoriety::kItemCount];  // datagroup "sr2_notoriety"
    DataItem hood;

    // Lookup by the dataitem's own name string; nullptr when unknown. The
    // notoriety datagroup's items are not reachable by name (the group is).
    DataItem* find(const std::string& name);
};

}  // namespace sr3hud
