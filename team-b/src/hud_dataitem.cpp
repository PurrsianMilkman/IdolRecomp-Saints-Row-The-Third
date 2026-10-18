// HUD dataitems - spec-hud-runtime.md H.1. See include/sr3hud/dataitem.h.

#include "sr3hud/dataitem.h"

#include <utility>

namespace sr3hud {

FieldType TypeOf(const FieldValue& v) {
    switch (v.index()) {
    case 0: return FieldType::Int;
    case 1: return FieldType::Uint;
    case 2: return FieldType::Float;
    case 3: return FieldType::String;
    default: return FieldType::Bool;
    }
}

FieldValue DefaultValue(FieldType t) {
    switch (t) {
    case FieldType::Int: return int32_t{0};
    case FieldType::Uint: return uint32_t{0};
    case FieldType::Float: return 0.0f;
    case FieldType::String: return std::string();
    case FieldType::Bool: return false;
    }
    return int32_t{0};
}

DataItem::DataItem(std::string name, std::vector<FieldDesc> fields)
    : name_(std::move(name)), fields_(std::move(fields)) {
    values_.reserve(fields_.size());
    for (const FieldDesc& f : fields_) values_.push_back(DefaultValue(f.type));
}

int DataItem::fieldIndex(const std::string& fieldName) const {
    for (size_t i = 0; i < fields_.size(); ++i)
        if (fields_[i].name == fieldName) return static_cast<int>(i);
    return -1;
}

bool DataItem::set(size_t i, const FieldValue& v) {
    if (i >= fields_.size()) return false;
    if (TypeOf(v) != fields_[i].type) return false;
    if (values_[i] == v) return false;  // equal: return at once, item stays clean
    values_[i] = v;
    dirty_ = true;
    return true;
}

bool DataItem::consumeDirty() {
    bool d = dirty_;
    dirty_ = false;
    return d;
}

namespace {

using T = FieldType;

DataItem MakeStatus() {
    // H.1, 13 fields, types as listed in the spec.
    return DataItem("sr2_local_player_status", {
        {"sprint_enabled", T::Bool},       {"health_pct", T::Float},
        {"health_level", T::Int},          {"is_dbno", T::Bool},
        {"bleed_out_health_pct", T::Float}, {"sprint_pct", T::Float},
        {"sprint_level", T::Int},          {"is_sprinting", T::Bool},
        {"orientation", T::Float},         {"is_mp_invulnerable", T::Bool},
        {"tunnel_vision_amount", T::Float}, {"is_in_satellite_mode", T::Bool},
        {"is_controlling_missile", T::Bool},
    });
}

DataItem MakeInfrequent() {
    return DataItem("sr2_local_player_status_infrequent", {
        {"cash", T::Float},                {"max_followers", T::Uint},
        {"inventory_disabled", T::Bool},   {"vehicle_logo", T::String},
        {"radio_station", T::String},      {"cruise_control_active", T::Bool},
        {"camera_screenshot_enabled", T::Bool}, {"force_show_cash", T::Bool},
        {"show_vtol_hints", T::Bool},
    });
}

DataItem MakeRespect() {
    return DataItem("sr2_local_player_respect", {
        {"total_respect", T::Int}, {"respect_pct", T::Float},
        {"respect_level", T::Int}, {"show_upgrades", T::Bool},
        {"force_show_respect", T::Bool},
    });
}

DataItem MakeWeapons() {
    // The spec gives names for fields 0-4 and 6-11 but no type codes for
    // this item. Bitmap names are strings (H.2 calls field 0 a string);
    // counts are integers; the yes/no flags are bools - CHOSEN from how
    // H.2 describes each field's use. Unnamed fields are placeholders.
    std::vector<FieldDesc> f;
    for (size_t i = 0; i < weapons::Count; ++i)
        f.push_back({"field_" + std::to_string(i), T::Int});
    f[weapons::WpnBmpName] = {"wpn_bmp_name", T::String};
    f[weapons::RdyAmmo] = {"rdy_ammo", T::Int};
    f[weapons::RsvAmmo] = {"rsv_ammo", T::Int};
    f[weapons::GrenadeBmpName] = {"grenade_bmp_name", T::String};
    f[weapons::GrenadeAmmo] = {"grenade_ammo", T::Int};
    f[weapons::DualWield] = {"dual_wield", T::Bool};
    f[weapons::NoAmmo] = {"no_ammo", T::Bool};
    f[weapons::InfAmmo] = {"inf_ammo", T::Bool};
    f[weapons::GndInfAmmo] = {"gnd_inf_ammo", T::Bool};
    f[weapons::ContFire] = {"cont_fire", T::Bool};
    f[weapons::NoMag] = {"no_mag", T::Bool};
    return DataItem("sr2_local_player_weapons", std::move(f));
}

DataItem MakeFrequentWeapon() {
    // Names from H.1; no type codes given. fine_aim_transition is used as a
    // float by the map-alpha formula (H.2); the rest are CHOSEN.
    return DataItem("sr2_local_player_frequent_weapon", {
        {"ammo_ready", T::Bool},           {"reticle_highlight", T::Int},
        {"cur_wpn_spread", T::Float},      {"fine_aim_transition", T::Float},
        {"water_pressure", T::Float},      {"x_screen_coord", T::Float},
        {"y_screen_coord", T::Float},      {"reticule_opacity", T::Float},
    });
}

DataItem MakeNotorietyItem(size_t i) {
    // H.1: team_name(4) noto_level(3) noto_icon(4, always "").
    return DataItem("sr2_notoriety[" + std::to_string(i) + "]", {
        {"team_name", T::String}, {"noto_level", T::Float}, {"noto_icon", T::String},
    });
}

DataItem MakeHood() {
    // Names from H.1; types CHOSEN (Lua does nothing with this item).
    return DataItem("sr2_local_player_hood", {
        {"hood_name", T::String}, {"cur_hood_owner", T::String},
        {"orig_hood_owner", T::String}, {"is_contested", T::Bool},
    });
}

}  // namespace

HudDataItems::HudDataItems()
    : status(MakeStatus()),
      infrequent(MakeInfrequent()),
      respect(MakeRespect()),
      weapons(MakeWeapons()),
      frequentWeapon(MakeFrequentWeapon()),
      notoriety{MakeNotorietyItem(0), MakeNotorietyItem(1), MakeNotorietyItem(2),
                MakeNotorietyItem(3)},
      hood(MakeHood()) {}

DataItem* HudDataItems::find(const std::string& name) {
    for (DataItem* d : {&status, &infrequent, &respect, &weapons, &frequentWeapon, &hood})
        if (d->name() == name) return d;
    return nullptr;
}

}  // namespace sr3hud
