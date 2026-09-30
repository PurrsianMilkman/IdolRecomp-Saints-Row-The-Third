#pragma once

// camera_free.xtbl - the gameplay camera parameter table
// (spec-tables-environment.md 12.2). Included from
// sr3tables_environment/tables.h; split out purely for size.
//
// Whole-file: Table -> ONE <Camera> element with eight named groups; nothing
// is per-row except the three repeated arrays (submodes/submode,
// vehicle_fallbacks/vehicle_fallback, Vehicle_Aims/Vehicle_Aim).
//
// Unlike most other tables in this group, the spec does not state per-field
// whether each panning_group/asvct_group/... scalar uses the "always write"
// or "write only if present" accessor (12.2 gives names and destinations,
// not accessor identities, for this table specifically). This reader models
// them as Always<float>, matching this group's general convention (most
// bare scalar elements elsewhere in this spec ARE always-write) - this is a
// MODELLING CHOICE, not a spec-stated fact, and is called out here plus
// flagged again in the validation harness report.

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3tables_environment {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;
using sr3xtbl::Vec3Result;

struct CameraFreePanningGroup {
    Always<float> fastPanHorizontalMultiplier, fastPanVerticalMultiplier;
    Always<float> slowPanHorizontalMultiplier, slowPanVerticalMultiplier;
    Always<float> pegAccelPanHorizontalMin, pegAccelPanHorizontalMax;
    Always<float> pegAccelPanVerticalMin, pegAccelPanVerticalMax;
    Always<float> zoomScaleMin, zoomScaleMax; // read in the opposite order in memory (spec 12.2); no effect here
    Always<float> fastPanInputThreshold;
    Always<float> accelScale, decelScale;
    Always<float> fineAimAccelScale, fineAimDecelScale;
    Always<float> hpanThreshold, vpanThreshold;

    struct Dampening {
        Always<float> horiz, vert;
    };
    // {fine_aim, heavy_weapon, human_shield, tank, tank_..._genki} x {horiz, vert}_dampening
    Dampening fineAimDampening, heavyWeaponDampening, humanShieldDampening, tankDampening, tankGenkiDampening;
    Always<float> interiorVDampening, interiorHDampening; // interior_v_dampening, interior_h_dampening
    Always<float> skydiveHDampening, skydiveVDampening;   // skydive_h_dampening, skydive_v_dampening
    Always<float> freefallHDampening;                     // freefall_h_dampening (no _v in the spec text)
    Always<float> parachuteHDampening;                    // parachute_h_dampening (no _v)
    Always<float> helicopterHDampening;                   // helicopter_h_dampening (no _v)

    // The same full set again, each with a "_mouse" suffix (mouse-input
    // dampening; spec 12.2 gives two concrete examples -
    // interior_v_dampening_mouse / interior_h_dampening_mouse - and states
    // "the same set with a _mouse suffix" for the rest).
    Dampening fineAimDampeningMouse, heavyWeaponDampeningMouse, humanShieldDampeningMouse,
        tankDampeningMouse, tankGenkiDampeningMouse;
    Always<float> interiorVDampeningMouse, interiorHDampeningMouse;
    Always<float> skydiveHDampeningMouse, skydiveVDampeningMouse;
    Always<float> freefallHDampeningMouse;
    Always<float> parachuteHDampeningMouse;
    Always<float> helicopterHDampeningMouse;
};

struct CameraFreeAsvctGroup {
    Always<float> defaultTime, stationaryTime, stationaryThreshold;
};
struct CameraFreeFollowAggressionGroup {
    Always<float> defaultSwingRate;
};
struct CameraFreeHillTrackingGroup {
    Always<float> defaultAggression, genkiAggression;
};
struct CameraFreeMiscellanyGroup {
    Always<float> pitchResetTime;      // pitch_reset_time
    Always<float> backawaySpeed;       // backaway_speed
    Always<float> manualAimElevationAngle;         // Manual_Aim_Elevation_Angle
    Always<float> manualVehicleAimElevationAngle;  // Manual_Vehicle_Aim_Elevation_Angle
    Always<float> helicopterLandingPitch;          // Helicopter_Landing_Pitch
    Always<float> helicopterLandingMaxSpeed;       // Helicopter_Landing_Max_Speed
    Always<float> helicopterLandingMaxAltitude;    // Helicopter_Landing_Max_Altitude
    Always<float> exteriorInteriorBlendTime;       // Exterior_Interior_Blend_Time
    Always<float> ragdollBlendTime;                // Ragdoll_Blend_Time
};

// The 61-entry submode name table (spec 12.2/13), in the order the spec
// lists them (index = the selected record's slot).
inline constexpr std::array<std::string_view, 61> kCameraFreeSubmodeNames = {
    "exterior close", "interior close", "interior sprint", "vehicle driver", "vehicle driver alt",
    "watercraft driver", "helicopter driver", "vehicle aim", "vehicle fine aim", "vehicle zoom",
    "airplane driver", "tank driver", "zoom", "zoom crouch", "swimming", "spectator", "fence",
    "ragdoll", "falling", "leaping", "fine aim", "fine aim underslung", "fine aim crouch",
    "melee lock", "fine aim vehicle", "freefall", "parachute", "riot shield",
    "riot shield fine aim", "human shield", "human shield fine aim", "sprint", "crouch",
    "crouch interior", "avatar", "shooting qte", "wrestling", "interior melee", "rc vehicle",
    "rc watercraft", "rc helicopter", "rc airplane", "rc tank", "downed", "skydiving down",
    "skydiving down fine aim", "skydiving dive", "skydiving dive fine aim", "skydiving up",
    "skydiving up fine aim", "skydiving tank", "skydiving tank gunner",
    "skydiving tank bail out", "rappelling", "rappelling fine aim", "rappelling zoom", "Script",
    "Script Fine Aim", "Script Crouch", "Script Fine Aim Crouch", "MAS Fine Aim",
};

struct CameraFreeSubmode {
    std::optional<std::string> name;    // name -> kCameraFreeSubmodeNames index (case-insensitive)
    std::optional<std::string> aspect;  // Aspect (string, read; destination not stated by the spec)
    Vec3Result lookatOffset;             // lookat_offset (vec3), combined with z_dist/y_dist at load
    std::optional<float> zDist, yDist;   // z_dist, y_dist
    Always<float> minElevation;          // min_elevation -> +0x10
    Always<float> maxElevation;          // max_elevation -> +0x14
    std::optional<float> defaultElevation; // default_elevation -> +0x1C (EXPLICITLY write-if-present)
    Always<float> baseFov;                // base_fov -> +0x20
    Always<float> blendTime;              // blend_time -> +0x2C
    Always<float> xShift;                 // x_shift -> +0x34
    std::optional<bool> overrideExitBlendTime; // override_exit_blend_time -> +0x30
};

// The 7-entry vehicle-fallback name table (spec 12.2/13).
inline constexpr std::array<std::string_view, 7> kCameraFreeVehicleFallbackNames = {
    "nitrous", "burnout", "stunt cam 1", "stunt cam 3", "stunt cam 4", "parachute open", "chainsaw",
};

struct CameraFreeVehicleFallback {
    std::optional<std::string> name; // name -> kCameraFreeVehicleFallbackNames index
    Always<float> distance;           // distance -> +0x00
    Always<int32_t> rampin;           // rampin -> +0x04
    Always<int32_t> duration;         // duration -> +0x08
    Always<int32_t> returnValue;      // "return" -> +0x0C (renamed: `return` is a C++ keyword)
};

struct CameraFreeVehicleAim {
    std::optional<std::string> name;    // name
    std::optional<std::string> aspect;  // Aspect
    Vec3Result lookatOffset;             // Lookat_Offset (vec3)
    Always<float> minPitch;              // Min_Pitch -> +0x20
    Always<float> maxPitch;              // Max_pitch -> +0x24 (spelling as in the spec: lower-case "pitch")
    Always<float> xShift;                // X_Shift -> +0x28
    Always<float> headingRange;          // Heading_Range -> +0x2C
    Always<float> headingCenter;         // Heading_Center -> +0x30
    Always<float> baseFov;               // base_fov -> +0x34
    Always<float> zDist;                 // z_dist -> +0x38
    Always<float> yDist;                 // y_dist -> +0x3C
    std::vector<std::string> flagNames;  // flags/Flag (repeated; name->bit map NOT decoded by the spec
                                          // (OPEN) - raw flag name text kept, no bit meaning assigned)
};

struct CameraFree {
    CameraFreePanningGroup panning;                           // panning_group
    CameraFreeAsvctGroup asvct;                                // asvct_group
    CameraFreeFollowAggressionGroup followAggression;          // follow_aggression_group
    CameraFreeHillTrackingGroup hillTracking;                  // hill_tracking_group
    CameraFreeMiscellanyGroup miscellany;                       // miscellany_group
    std::vector<CameraFreeSubmode> submodes;                    // submodes/submode
    std::vector<CameraFreeVehicleFallback> vehicleFallbacks;    // vehicle_fallbacks/vehicle_fallback
    std::vector<CameraFreeVehicleAim> vehicleAims;              // Vehicle_Aims/Vehicle_Aim
};

// Returns std::nullopt if the file has no <Camera> element under Table.
std::optional<CameraFree> ParseCameraFree(const Document& doc);

}  // namespace sr3tables_environment
