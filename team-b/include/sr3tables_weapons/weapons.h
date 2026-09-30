#pragma once

// sr3tables_weapons - typed reader for `weapons.xtbl` and `weapon_categories.xtbl`
// (spec-tables-weapons-combat.md section 2 and section 3).
//
// Built entirely on sr3xtbl's accessors (include/sr3xtbl/xtbl.h): FindChild,
// ChildText, GetX/ReadXAlways, ReadVec3[Child], FlagMask/HasFlag, EnumIndex,
// NameHash. Nothing here touches the parser itself.
//
// ---------------------------------------------------------------------------
// CONVENTIONS USED THROUGHOUT (documented once, applied consistently)
// ---------------------------------------------------------------------------
// 1. "always write" elements -> sr3xtbl::Always<T> (see xtbl.h: value is a
//    deterministic 0 stand-in when !present, NOT the engine's real default;
//    callers must check `present`). "write only if present" elements ->
//    std::optional<T>. The spec marks most fields explicitly with "(a)" /
//    "(i)"; those markers are followed exactly. Where section 2.2/2.3 gives
//    no explicit (a)/(i) marker for a scalar, this reader defaults to the
//    Always<T> family (matching the document's own general description of
//    the reader grammar, section 1.3: absent yields an unspecified/0
//    stand-in) EXCEPT where the spec's prose explicitly says "written only
//    if present" (Category, Zoom_Type, Strafe_Angles), which is modelled as
//    optional<T>. Each such judgement call is noted per-field below; none of
//    these are spec/real-data disagreements - they are this reader's
//    resolution of an unmarked cell, reported to the caller for awareness.
// 2. Cross-table name resolution (Animation_Group index, Ammo/Ambient
//    pointers, effect handles, explosion/tracer/brass pointers, items_3d /
//    items_inventory records, camera-shake indices, Wwise ids, physical-
//    material indices, animation-state indices, ...) is OUT OF SCOPE: this
//    project has no spec for those companion tables' storage, so this
//    reader stores the raw NAME TEXT the row supplies (std::optional
//    <std::string>) rather than inventing a resolved index/pointer. Each
//    such field is commented "(name text; resolver out of scope)".
// 3. Where the record layout documents a stored TRANSFORM of the XML value
//    (degrees -> radians, seconds -> milliseconds, a reciprocal, a square,
//    a CRC of a name) this reader applies that single documented transform,
//    because that transform IS the schema (matching what the offset holds).
//    It does NOT reproduce transforms that combine several XML elements into
//    a derived geometric result with no further schema value here (e.g. the
//    six Fire_Cone_Metrics ring points, ImpactDir normalisation) - the raw
//    inputs are kept instead and the spec section is cited so a caller who
//    needs the derived geometry can compute it.
// 4. Fields the spec marks OPEN/UNKNOWN, or whose destination this project
//    cannot resolve because a dependency table has no spec here (the 33
//    physical-material names of FUN_006F76F0, the animation-group/state
//    name arrays, the panic-reaction/groundfire id tables, ...), are left
//    out of the typed struct or kept as raw text, per the task's "don't
//    guess" rule. See the accompanying report for the full list.
// 5. The single documented engine degrees->radians constant this project
//    uses everywhere the spec calls for that conversion is the one spelled
//    out for the spread blocks (section 2.3): 0.01745299994945526f (NOT the
//    literal float32 nearest pi/180, which differs in the last bit).
//
// Record-management concerns that live in the RUNTIME array (not a single
// row) are intentionally not modelled: the "loaded" bit (record +0x18 bit
// 2), the Base_Version post-pass pointer resolution, the DLC slot-index
// bookkeeping, and weapon_upgrades patch application (section 4) all need
// the whole loaded array and are out of scope for a per-row/per-file typed
// reader.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3tables_weapons {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;
using sr3xtbl::Vec3Result;

// The one engine degrees->radians constant this reader uses (spec section 2.3).
inline constexpr float kDegToRad = 0.01745299994945526f;

// ---------------------------------------------------------------------------
// Enumerations (spec section 2.5). Index into the array == the stored value.
// ---------------------------------------------------------------------------
inline constexpr std::string_view kWeaponClassNames[23] = {
    "pistol", "smg", "rifle", "shotgun", "launcher", "thrown", "knife", "nightstick",
    "stungun", "bat", "sword", "pimp slap", "video camera", "knuckles", "flamethrower",
    "cutscene only", "man cannon", "minigun", "pepper spray", "chainsaw", "waterspray",
    "script", "vehicle",
};
inline constexpr std::string_view kInvSlotNames[11] = {
    "unarmed", "melee", "pistol", "smg", "shotgun", "rifle", "explosive", "special",
    "vehicle", "grenade", "single_use",
};
inline constexpr std::string_view kWeaponCategoryNames[7] = {
    "WPNCAT_MELEE", "WPNCAT_PISTOL", "WPNCAT_SUB_MACHINE_GUN", "WPNCAT_SHOTGUN",
    "WPNCAT_RIFLE", "WPNCAT_THROWN", "WPNCAT_SPECIAL",
};
inline constexpr std::string_view kTriggerTypeNames[4] = {"single", "burst", "automatic", "charge release"};
inline constexpr std::string_view kGrenadeTypeNames[4] = {"standard", "stun", "flashbang", "molotov"};
inline constexpr std::string_view kSpecialCaseTypeNames[14] = {
    "RC Gun", "Air Strike", "Sonic Wave Gun", "Riot Shield", "Needler Prototype",
    "Flak Cannon Prototype", "Multi Launcher Prototype", "Satellite Drone Prototype",
    "Wieldable Prop Weapon", "Avatar Sword", "Heli Spotlight", "Cyber Cannon",
    "Flamethrower", "Chum",
};
inline constexpr int kSpecialCaseWieldableProp = 8;  // "Wieldable Prop Weapon" gates two fields (section 2.2)
inline constexpr std::string_view kEffectSituationNames[25] = {
    "muzzle flash", "alt muzzle flash", "tracer", "alt tracer", "bullet impact override",
    "alt bullet impact override", "overheat", "player flashlight", "npc flashlight",
    "charge release charging muzzle", "charge release charging ribbon",
    "charge release charging target", "laser cutter ribbon", "laser cutter target near",
    "laser cutter target far", "laser guide ribbon", "laser guide target", "projectile",
    "projectile ignite", "projectile post ignition", "projectile create",
    "sonic hit effect", "sonic scan charging", "sonic scan charged", "genki fire",
};

// Word A flag literals (spec section 2.4), positioned by bit index; the two
// gaps (bit 0x8000000 set by Waterspray_info, bit 0x80000000 the runtime
// "unlocked" bit set by crib_weapons.xtbl) are "" placeholders - no <Flag>
// text can ever match an empty name (an empty element has no text at all
// per the document model), so FlagMask never sets those bits from text.
inline constexpr std::string_view kFlagsAWordNames[32] = {
    "alt unlimited ammo", "infinite magazine capacity", "heavy weapon move speed", "unlimited ammo",
    "other hand ik during attack only", "no combat ready", "no vehicle combat ready", "use block flinch anims",
    "on selection", "on trigger", "lethal melee", "bullets damage tanks",
    "explosions damage tanks", "melee can dislodge movers", "one shot shatter", "allow offhand grenade",
    "attaches", "brass during reload only", "dual wieldable", "can zoom",
    "disallow forward throw in vehicle", "has alt fire", "melee continue on world collide", "use modified bullet direction",
    "manually detonates", "revives humans", "secondary trigger fires grenades", "",
    "secondary weapon", "use box shape for melee casts", "use underslung fine aim", "",
};
// Word B: bit 0x4000 (index 14) has no literal in the spec (a gap, not
// "set elsewhere" like word A's gaps - just absent from the enumeration).
inline constexpr std::string_view kFlagsBWordNames[32] = {
    "cutscene only", "disallowed in demos", "not allowed in vehicle", "not allowed with human shield",
    "unlockable", "disallow jumping", "disallow crouching", "disallow reload",
    "apply force to live ragdolls", "instant ragdoll", "causes convulsions", "left hand",
    "always wear on back", "do not hide when sprinting", "", "always play melee vfx",
    "constant vfx on combat ready only", "constant vfx only at night", "looping muzzle flash", "use mission srid for effects",
    "bullets can hit multiple humans", "no blood splat", "no random give", "incendiary shots",
    "armor piercing override", "gibs victims", "zoom allows fine aim", "attach to forearm",
    "show reserve in hud", "drops with full reserve", "player instant reload", "melee always dislodge",
};
// Word C: only bits 0/1 are Flag-text-settable; bit 2 (0x4, "loaded") is
// set by the loader on the live runtime array, not by <Flag> text - a
// single-row parser has no "loaded" state to compute, so it is not modelled.
inline constexpr std::string_view kFlagsCWordNames[2] = {"no bullet decal", "manned turret"};

// Projectile_Flags (spec section 2.4): bit 0x10000 (index 16) is set by the
// presence of Fuse_Time, not by Flag text - "" placeholder, same reasoning.
inline constexpr std::string_view kProjectileFlagNames[25] = {
    "has light attached", "rocket flight", "sticky", "harpoon", "satchel charge",
    "guided", "guided on fine aim", "attach effect after ignition", "detonate on vehicle collision",
    "detonate on human collision", "use bullet collision quality", "orient projectile to velocity",
    "dont detonate from explosion", "seek to target pos", "seek to target pos (npc only)",
    "does not fire from muzzle", "", "swarm", "vehicle rc", "teleport to target",
    "rc self destruct", "rc military allowed", "play attach sound on vehicles only",
    "show hud indicator", "genki",
};

// Overheat_Flags (spec section 2.3/2.4): contiguous, bit i == array index i.
inline constexpr std::string_view kOverheatFlagNames[3] = {"applies to primary", "applies to alt fire", "play reload anim"};

// Component flags of a Vehicle_Weapon component (spec section 2.3).
inline constexpr std::string_view kVehicleWeaponComponentFlagNames[2] = {"target reticule", "enable physics"};
inline constexpr std::string_view kTurretPartFlagNames[1] = {"don't reset angle when unmanned"};

// ---------------------------------------------------------------------------
// Sub-blocks (spec section 2.3)
// ---------------------------------------------------------------------------

// `Melee_Damage_Overrides` (+0x20..+0x2C). `present` mirrors the +0x20 byte.
struct MeleeDamageOverrides {
    bool present = false;
    Always<float> npc;     // +0x24 NPC
    Always<float> player;  // +0x28 Player
    Always<float> online;  // +0x2C Online
};

// `Tracer_Info` (+0x78..+0x88). All four pointer fields are name text here
// (resolver FUN_005A2FD0 -> weapon_tracers.xtbl is out of scope for a
// single-row parse; see tables in combat.h for weapon_tracers.xtbl itself).
struct TracerInfo {
    bool present = false;
    std::optional<std::string> tracer;         // Tracer
    std::optional<std::string> tracerNpc;       // Tracer_NPC
    std::optional<std::string> altTracer;       // Alt_Tracer
    std::optional<std::string> altTracerNpc;    // Alt_Tracer_NPC
    Always<uint8_t> tracerFrequency;            // Tracer_Frequency, +0x88, default 0
};

// One `Constant_Effect` (+0x8C + 12*i .. capacity 4, count at +0xBC).
struct ConstantEffect {
    std::optional<std::string> effect;           // Effect (name text; resolver out of scope)
    std::optional<std::string> weaponPropPoint;   // Weapon_Prop_Point, first load only
    // Condition: "during melee swing"=1, "fine aim"=2, "weapon raised"=3,
    // anything else INCLUDING ABSENT or unrecognised text (e.g. DLC3's
    // "always on") = 0. NOT sr3xtbl::EnumIndex (which would give -1).
    int condition = 0;
};

// `Audio` block (+0xC8..+0xDD). Written only if the block exists.
// Weapon_Model/Soundbank_Name/*_Event are Wwise event/bank NAMES, not the
// ids the engine computes through FUN_00462960 (undocumented hash, out of
// scope) - stored as name text.
struct AudioInfo {
    bool present = false;
    std::optional<std::string> weaponModel;              // Weapon_Model
    std::optional<std::string> soundbankName;             // Soundbank_Name; default "Wep_<Weapon_Model>" (not computed here)
    std::optional<std::string> stopOverrideEvent;          // Stop_Override_Event; default 0/none
    std::optional<std::string> altFireStopOverrideEvent;    // Alt_Fire_Stop_Override_Event; default 0/none
    std::optional<float> soundRadius;                       // Sound_Radius (i); 0 on first load if absent
    std::optional<bool> looping;                             // looping; no stated default
    std::optional<bool> altLooping;                           // alt_looping; default 0 if absent
};

// `Target_Lockon` (+0xE0..+0x104). `present` is the +0xE0 byte.
struct TargetLockon {
    bool present = false;
    Always<int32_t> lockonTimeMs;                    // Lockon_Time_MS
    Always<float> lockingSizeMultiplier;               // Locking_Size_Multiplier
    Always<float> lockedSizeMultiplier;                 // Locked_Size_Multiplier
    Always<float> beepTimingSlowest;                     // Beep_Timing_Slowest
    Always<float> beepTimingFastest;                      // Beep_Timing_Fastest
    Always<float> angleFromReticle;                        // Angle_From_Reticle
    Always<float> angleFromReticleLoseTarget;               // Angle_From_Reticle_Lose_Target
    Always<float> lockingRotationFurthestAngleRadians;       // Locking_Rotation_Furthest_Angle, deg->rad
    bool enemyAircraftOnly = false;                           // flags/flag bit 0
};

// `Trigger_Type`/`Alt_Trigger_Type` share the same 4-name enum.
// `Time_Management` / `Alt_Time_Management` (0x0E bytes each; +0x130 / +0x13E).
struct TimeManagementBlock {
    Always<uint16_t> refireDelayMs;        // Refire_Delay, read as u32, stored u16
    Always<uint16_t> npcRefireDelayMin;     // npc_refire_delay/min; documented default 400
    Always<uint16_t> npcRefireDelayMax;      // npc_refire_delay/max; documented default min+500, raised to >= min (not computed here)
    Always<uint16_t> postDetonateDelay;       // Post_Detonate_Delay; 0 if absent (first load)
    Always<uint16_t> preDetonateDelay;         // Pre_Detonate_Delay; 0 if absent (first load)
    Always<uint16_t> firearmRampInTimeMs;       // Firecone_Ramp_In_Time (seconds authored, ms stored); 0 if absent
    uint8_t npcRefireType = 0;                   // npc_refire_delay/npc_refire_type: first char - '1'; 0 if absent/empty
};

// Spread blocks (spec section 2.3). "Every multiplier is an 'if present'
// float ... default 1.0 stands [when absent]"; the non-multiplier fields
// ("all others") default 0, undecorated -> Always<T> by this reader's
// convention.
struct SpreadMovementMultipliersPlayer {
    std::optional<float> crouch, walk, run, sprint, vehicle, fineAim;  // default 1.0 each if absent
};
struct SpreadMovementMultipliersNpc {
    std::optional<float> crouch, walk, run, sprint, vehicle;  // default 1.0 each if absent (no Fine_Aim for NPC)
};
struct SpreadTargetMovementMultipliersNpc {
    std::optional<float> walk, run, sprint, vehicle;  // default 1.0 each if absent
};
struct PlayerSpreadBlock {  // `PlayerWeaponSpread` / `PlayerAltWeaponSpread`, 0x3C bytes
    Always<float> spreadMinRadians;                          // SpreadMinMax/Player_Spread_Min, deg->rad
    Always<float> spreadMaxRadians;                           // SpreadMinMax/Player_Spread_Max, deg->rad
    SpreadMovementMultipliersPlayer movementMultipliers;       // SpreadMovementMultipliers/Movement_Multiplier_*
    Always<uint8_t> toSpreadMax;                                 // SpreadDynamics/Player_To_Spread_Max
    Always<uint16_t> toSpreadMin;                                 // SpreadDynamics/Player_To_Spread_Min
    SpreadMovementMultipliersPlayer dynamicMultipliers;            // SpreadDynamics/SpreadDynamicMultipliers/Movement_Multiplier_*
};
struct NpcSpreadBlock {  // `NPCWeaponSpread` / `NPCAltWeaponSpread`, 0x4C bytes
    Always<float> spreadMinRadians;                                // SpreadMinMax/NPC_Spread_Min, deg->rad
    Always<float> spreadMaxRadians;                                 // SpreadMinMax/NPC_Spread_Max, deg->rad
    SpreadMovementMultipliersNpc movementMultipliers;                 // SpreadMovementMultipliers/Movement_Multiplier_*
    Always<uint8_t> toSpreadMax;                                       // SpreadDynamics/NPC_To_Spread_Max
    Always<uint16_t> toSpreadMin;                                       // SpreadDynamics/NPC_To_Spread_Min
    SpreadMovementMultipliersNpc dynamicMultipliers;                     // SpreadDynamics/SpreadDynamicMultipliers/Movement_Multiplier_*
    SpreadTargetMovementMultipliersNpc targetMovementMultipliers;         // SpreadMinMax/SpreadTargetMovementMultipliers/Target_Movement_Multiplier_*
};

// `Flat_Spread_Metrics` (+0x190..+0x1A0). `present` is the +0x190 byte.
struct FlatSpreadMetrics {
    bool present = false;
    Always<float> widthAngleRadians;          // Flat_Spread_Width_Angle, deg->rad
    Always<float> heightAngleRadians;          // Flat_Spread_Height_Angle, deg->rad
    Always<float> rotationRadians;              // Flat_Spread_Rotation, deg->rad
    Always<float> rotationPerShotRadians;        // Flat_Spread_Rotation_Per_Shot, deg->rad
};

// `Fire_Cone_Metrics` (+0x1AC..+0x1BC + 6 ring points, spec section 2.3).
// Only the two authored forms are kept; the derived ring geometry
// (Near/Far -> six 60-degree points) is NOT reproduced here (convention 3).
struct FireConeMetrics {
    bool present = false;        // whether <Fire_Cone_Metrics> existed at all
    bool isMinMaxForm = false;   // Metric_Type/Min_Max present (else the Angle form, if present)
    std::optional<float> angleDegrees;  // Metric_Type/Angle/Angle (Angle form); stored value at +0x1A4 is cos(half-angle) - see fireConeAngleCos on Weapon
    std::optional<float> nearRadius;    // Metric_Type/Min_Max/Near_Radius
    std::optional<float> farRadius;     // Metric_Type/Min_Max/Far_Radius
};

// `Ragdoll_Info` (+0x328..+0x344). All "always" floats, zero if absent.
struct RagdollInfo {
    Always<float> chance;
    Always<float> deathVelocityHorizontal;
    Always<float> deathVelocityVertical;
    Always<float> deathPointVelocity;
    Always<float> deathAngularVelocityHorizontal;
    Always<float> deathAngularVelocityVertical;
    Always<float> deathRangeMin;
    Always<float> deathRangeMax;
};

// `Projectile_Info` (+0x348..+0x3AC, 0x68 bytes). The whole block is
// skipped if absent (`present` false). `Model` failing to resolve in
// items_3d.xtbl fails the whole weapon row in the real loader - out of
// scope to detect (no items_3d spec here).
struct ProjectileInfo {
    bool present = false;
    std::optional<std::string> model;              // Model (items_3d name text; resolver out of scope)
    Always<float> speed;                             // Speed
    Always<float> speedNpc;                           // Speed_NPC; NPC defaults to Speed
    Always<float> postIgnitionSpeed;                    // Post_Ignition_Speed
    Always<float> postIgnitionSpeedNpc;                  // Post_Ignition_Speed_NPC; NPC defaults to the former
    Always<float> gravity;                                // Gravity; documented default -9.8
    Always<float> launchPitchChangeRadians;                // Launch_pitch_change_deg, deg->rad
    std::optional<std::string> attachedEffect;              // Attached_Effect (name text); -1 if absent
    std::optional<std::string> creationEffect;               // Creation_Effect (name text); -1 if absent
    std::optional<std::string> attachedEffectPropPoint;       // Attached_Effect_Prop_Point, first load only
    Always<uint16_t> fuseTimeMs;                                // Fuse_Time; presence sets Projectile_Flags bit 0x10000
    Always<uint16_t> npcFuseTimeMs;                              // NPC_Fuse_Time; NPC defaults to the plain fuse
    Always<uint16_t> fadeOutTimeMs;                               // Fade_Out_Time (seconds authored, ms stored); 0 if absent/negative
    std::optional<int16_t> ignitionDelayMs;                        // Projectile_Ignition_Delay_MS (i)
    std::optional<uint16_t> aiCanGuide;                             // AI_Can_Guide (i), default 0
    Always<float> mass;                                              // Mass (a)
    std::optional<float> linearDamp;                                  // Linear_Damp (i), default 0
    std::optional<float> angularDamp;                                  // Angular_Damp (i), default 0
    std::optional<float> restitution;                                   // Restitution (i), default 0.4
    std::optional<float> friction;                                       // Friction (i), default 0.4
    Vec3Result angularVelocity;                                           // Angular_Velocity/X,Y,Z; wrapper required
    std::optional<float> blowTireRadius;                                   // Blow_Tire_Radius (i)
    std::optional<std::string> ignitionEffect;                             // Ignition_Effect (name text); -1 if absent
    std::optional<std::string> sound;                                       // Sound (Wwise name text); 0 if absent
    std::optional<std::string> foleyCollision;                              // FoleyCollision (name text); 0 if absent
    uint32_t projectileFlags = 0;                                            // Projectile_Flags/Flag bitmask (+ bit 0x10000 if Fuse_Time present)
};

// `Waterspray_info` (+0x444..+0x45C).
struct WaterSprayInfo {
    Always<float> waterStreamForce;             // Water_stream_force
    Always<float> refillRatePerSecond;           // Refill_rate_per_Second
    Always<float> radiusExpansionRate;            // Radius_Expansion_Rate
    uint32_t waterSprayEffectHash = 0;             // Waterspray_Effect: CRC of the name (0 if absent - NameHashOrZero)
    std::optional<float> pressureIncreaseRate;      // Pressure_Increase_Rate; presence sets Flags-A bit 0x8000000
    std::optional<float> pressureDecreaseRate;       // Pressure_Decrease_Rate (only read if Pressure_Increase_Rate present)
    std::optional<float> pressureRestoreTime;         // Pressure_Restore_Time (only read if Pressure_Increase_Rate present)
};

// `Charge_Release_Info` (+0x460..+0x484). Zero if absent.
struct ChargeReleaseInfo {
    std::optional<float> chargeTimeReciprocal;    // Charge_Time_sec -> stored as 1/seconds
    Always<float> minChargePercent;                // Min_Charge_Percent; documented default 1.0
    Always<float> chargeBase;                       // Charge_Base
    Always<bool> autoRelease;                        // Auto_Release
    Always<float> minRange;                           // Min_Range; documented default -1.0
    Always<float> preChargeDelay;                      // Pre_Charge_Delay
    std::optional<float> chargeCooldownReciprocal;      // Charge_Cooldown_Time -> 1/seconds; presence sets flag word bit 1
    std::optional<std::string> chargingCameraShake;      // Charging_Camera_Shake (name text)
    std::optional<std::string> chargedCameraShake;        // Charged_Camera_Shake (name text)
    bool showHudOnCharge = false;                           // Charge_Flags/Flag "Show HUD on charge", bit 0
};

// `Vehicle_Weapon` (+0x488..+0x70F).
struct TurretPart {  // Middle_Component / Top_Component, 0x20 bytes
    bool dontResetAngleWhenUnmanned = false;  // flag bit 0
    Always<float> minAngleRadians;              // Min_Angle, deg->rad
    Always<float> maxAngleRadians;               // Max_Angle, deg->rad
    Always<float> maxSpeedRadians;                // Max_Speed, deg->rad
    Always<float> dampAngleRadians;                // Damp_Angle, deg->rad
    Always<float> unmannedSpeedRadians;             // Unmanned_Speed, deg->rad
    std::optional<float> firingAngleSpeedRadians;    // Firing_Angle_Speed, deg->rad; defaults to Max_Speed
    Always<float> maxForceNewtons;                    // Max_Force, x4.448221683502197 (lbf -> N)
};
struct VehicleWeaponComponent {  // 0x50-byte component (FUN_00B7E3A0)
    Always<uint32_t> uid;                      // UID
    int32_t weaponClass = -1;                    // Weapon_Class, 23-name enum
    bool targetReticule = false;                  // flags bit 0
    bool enablePhysics = false;                     // flags bit 1
    std::optional<std::string> muzzleExplosion;       // Muzzle_Explosion (name text; resolver out of scope)
    TurretPart middleComponent;                         // Middle_Component
    TurretPart topComponent;                             // Top_Component
};
struct VehicleWeaponBlock {
    bool present = false;                       // any Vehicle_Weapon content (counts 0 if absent)
    std::vector<VehicleWeaponComponent> primaryWeapons;  // Primary_Weapons/Weapon, capacity 4
    std::vector<VehicleWeaponComponent> altWeapons;       // Alt_Weapons/Weapon, capacity 4
};

// `Camera_Info` (+0x710..+0x774).
struct CameraShakeWithIntensity {
    std::optional<std::string> shakeName;  // name text (resolver FUN_0057BEE0 out of scope)
    Always<float> intensity;                // documented default 1.0
};
struct CameraInfo {
    CameraShakeWithIntensity primaryFire;                  // Primary_Fire_Camera_Shake / _Intensity
    CameraShakeWithIntensity primaryFineAim;                 // Primary_Fine_Aim_Camera_Shake / Primary_Fire_Fine_Aim_Camera_Shake_Intensity
    CameraShakeWithIntensity secondaryFire;                    // Secondary_Fire_Camera_Shake / _Intensity
    CameraShakeWithIntensity secondaryFireFineAim;               // Secondary_Fire_Fine_Aim_Camera_Shake / _Intensity
    CameraShakeWithIntensity meleeHard;                            // Melee_Hard_Camera_Shake / _Intensity
    CameraShakeWithIntensity meleeSoft;                              // Melee_Soft_Camera_Shake / _Intensity
    CameraShakeWithIntensity playerHit;                                // Player_Hit_Camera_Shake / _Intensity
    std::optional<float> primaryRecoilMultiplier;                       // Primary_Recoil_Multiplier (if present)
    std::optional<float> primaryFineAimRecoilMultiplier;                  // Primary_Fine_Aim_Recoil_Multiplier (if present)
    Always<int32_t> primaryRecoilDelayMs;                                  // Primary_Recoil_Delay_ms
    Always<bool> primaryRecoilRamped;                                       // Primary_Recoil_Ramped; documented default true
    std::optional<float> secondaryRecoilMultiplier;                          // Secondary_Recoil_Multiplier (if present)
    std::optional<float> secondaryFineAimRecoilMultiplier;                    // Secondary_Fine_Aim_Recoil_Multiplier (if present)
    std::optional<int32_t> zoomType;                                          // Zoom_Type: progressive 0 / non-progressive 1; written only if present
    std::optional<float> minimumFov;                                          // Minimum_FOV (if present)
    std::optional<float> maximumFov;                                           // Maximum_FOV (if present)
    std::optional<float> zoomSteps;                                             // Zoom_Steps (if present)
    std::optional<float> fovRate;                                                // FOV_Rate (if present)
};

// `Overheat_Info` (+0x798..+0x7AC). Zero if absent.
struct OverheatInfo {
    Always<float> percentIncreasePerShot;
    Always<float> percentDecreasePerSecond;
    Always<float> percentDecreasePerReload;
    Always<float> percentDecreasePerSecondOverheated;
    Always<float> percentDecreasePerReloadOverheated;
    uint32_t overheatFlags = 0;  // Overheat_Flags/Flag bitmask (3 literals, contiguous)
};

// `Effect_Situations` (+0x7B0..+0x7CC). Positional: the first FOUR children
// regardless of their own tag, each read as Situation (enum) + Effect.
struct EffectSituationEntry {
    int situation = -1;                    // -1 if null/unmatched (sr3xtbl::EnumIndex)
    std::optional<std::string> effect;      // Effect (name text; resolver out of scope)
};

// `Burst_Fire_Info` (+0x774..+0x778).
struct BurstFireInfo {
    Always<int32_t> shots;
    Always<int32_t> burstDelayMs;
};

// ---------------------------------------------------------------------------
// weapons.xtbl - the `<Weapon>` row (spec section 2, record stride 0x7EC)
// ---------------------------------------------------------------------------
struct Weapon {
    // --- identity / framework (spec sections 1.5, 2.2) ---
    std::string name;                        // Name (+0x00), required
    std::optional<std::string> framework;      // Framework: absent/"main" = base-game row; else a DLC framework name
    std::optional<uint32_t> infoSlotIndex;      // Info_Slot_Index: parsed for information only - consumed by the DLC
                                                 // handler, never by this row's own reader (spec section 2, section 2.6)
    std::optional<std::string> baseVersion;      // Base_Version (+0x04): name text; the resolved-pointer post-pass (+0x08) is out of scope
    std::optional<bool> isDlc;                    // Is_DLC (+0x0C gate byte)

    // --- flags (+0x10/+0x14/+0x18, spec section 2.4) ---
    uint32_t flagsA = 0;   // word A, 30 Flag-text-settable bits (+ bit 0x8000000 via Waterspray_info, computed below)
    uint32_t flagsB = 0;   // word B, 31 Flag-text-settable bits
    uint32_t flagsC = 0;   // word C, 2 Flag-text-settable bits (bit 2 "loaded" is loader state, not modelled)

    int32_t specialCaseType = -1;              // Special_Case_Type (+0x1C), 14-name (value==position) table

    MeleeDamageOverrides meleeDamageOverrides;   // Melee_Damage_Overrides (+0x20..+0x2C)

    std::optional<std::string> animationGroup;       // Animation_Group (+0x30): name text; required, resolver out of scope
    std::optional<std::string> fineAimAnimationGroup;  // Fine_Aim_Animation_Group (+0x34); defaults to Animation_Group
    std::optional<std::string> reloadAnimationGroup;    // Reload_Animation_Group (+0x38); defaults to Animation_Group

    std::optional<float> reloadOverrideTimeSec;   // Reload_override_time_sec (+0x3C, i); default 0
    std::optional<uint16_t> warmupDelay;            // Warmup_Delay (+0x40, i); default 0
    std::optional<uint16_t> cooldownDelay;            // Cooldown_Delay (+0x42, i); default 0

    int32_t grenadeType = -1;                          // Grenade_Type (+0x44), 4-name enum
    std::optional<std::string> strafeAngles;             // Strafe_Angles (+0x48): name text; written only if present, else index 0
    int32_t weaponClass = -1;                              // Weapon_Class (+0x4C), 23-name enum
    std::optional<int32_t> category;                        // Category (+0x50): written only if present, else 0 (index 0 == WPNCAT_MELEE)
    uint8_t invSlot = 0xFF;                                   // Inv_Slot (+0x54), 11-name enum, 0xFF if absent/unmatched

    // +0x58/+0x5C (items_3d / items_inventory record pointers) and +0x60
    // (Muzzle_Flash, fetched and discarded, forced -1) are intentionally
    // not modelled (spec section 2.2: cross-table / no-effect fields).

    std::optional<std::string> muzzleEffect;      // Muzzle_Effect (+0x64, name text)
    std::optional<std::string> altMuzzleEffect;    // Alt_Muzzle_Effect (+0x68, name text)
    std::optional<std::string> meleeEffect;         // Melee_Effect (+0x6C, name text)
    std::optional<std::string> fireConeImpactEffect; // Fire_Cone_Impact_Effect (+0x70, name text)
    uint32_t offhandWeaponMeshHash = 0;                // Offhand_Weapon_Mesh (+0x74): CRC of the name text; 0 if absent

    TracerInfo tracerInfo;                               // Tracer_Info (+0x78..+0x88)
    std::vector<ConstantEffect> constantEffects;           // Constant_Effects/Constant_Effect (+0x8C.., capacity 4; count +0xBC)

    std::optional<std::string> brass;                        // Brass (+0xC0, name text)
    std::optional<float> diversionKillMultiplier;              // Diversion_Kill_Multiplier (+0xC4, i); default 1.0

    AudioInfo audio;                                              // Audio (+0xC8..+0xDD)

    TargetLockon targetLockon;                                      // Target_Lockon (+0xE0..+0x104)

    int32_t triggerType = -1;                                        // Trigger_Type (+0x108), 4-name enum
    int32_t altTriggerType = -1;                                       // Alt_Trigger_Type (+0x10C), 4-name enum

    std::optional<std::string> ammo;                                     // Ammo (+0x110, name text)
    std::optional<std::string> altAmmo;                                    // Alt_Ammo (+0x114, name text)
    Always<uint16_t> magazineSize;                                          // Magazine_Size (+0x118, a)
    Always<uint16_t> ammoPerShot;                                            // Ammo_per_Shot (+0x11A, a)
    Always<float> ammoRegeneration;                                           // Ammo_Regeneration (+0x11C, a)
    Always<float> rangeMax;                                                    // Range_Max (+0x120, a)
    Always<float> aiIdealRangeMin;                                              // AI_Ideal_Range_Min (+0x124, a)
    Always<float> aiIdealRangeMax;                                               // AI_Ideal_Range_Max (+0x128, a)

    std::optional<std::string> npcAimDrift;      // NPC_Aim_Drift (+0x12C): name text; default profile "Default"

    TimeManagementBlock timeManagement;             // Time_Management (+0x130)
    TimeManagementBlock altTimeManagement;            // Alt_Time_Management (+0x13E); all zero if absent

    Always<float> damageMaxNpc;                          // Damage_Max/NPC_Damage (+0x14C, a)
    Always<float> damageMaxPlayer;                        // Damage_Max/Player_Damage (+0x150, a); +0x154 is a redundant copy, not modelled
    bool damageMinPresent = false;                          // whether <Damage_Min> existed
    Always<float> damageMinNpc;                               // Damage_Min/NPC_Damage (+0x158, a); absent -> copies damageMaxNpc (not computed here)
    Always<float> damageMinPlayer;                              // Damage_Min/Player_Damage (+0x15C, a); absent -> copies damageMaxPlayer

    Always<float> riotShieldDamageMultiplier;    // Riot_Shield_Damage_Multiplier (+0x164, a)

    std::optional<std::string> explosion;         // Explosion (+0x168, name text)
    std::optional<std::string> altExplosion;        // Alt_Explosion (+0x16C); defaults to Explosion
    std::optional<std::string> npcExplosion;          // NPC_Explosion (+0x170); defaults to Explosion
    std::optional<std::string> npcAltExplosion;         // NPC_Alt_Explosion (+0x174); defaults to the (possibly overridden) NPC_Explosion
    std::optional<std::string> underwaterExplosion;      // Underwater_Explosion (+0x178)

    std::optional<float> damageMaxDist;    // Damage_Max_Dist (+0x17C, i); default 0
    std::optional<float> damageMinDist;     // Damage_Min_Dist (+0x180, i); default Range_Max
    std::optional<float> operatorDamageMultiplier;  // Operator_Damage_Multiplier (+0x184, i); default 1.0

    std::optional<std::string> wieldablePropDeathVfx;  // Wieldable_Prop_Death_VFX (+0x188): only meaningful when specialCaseType == 8
    std::optional<uint16_t> wieldablePropHitsAllowed;    // Wieldable_Prop_Hits_Allowed (+0x18C, i): same condition

    FlatSpreadMetrics flatSpreadMetrics;      // Flat_Spread_Metrics (+0x190..+0x1A0)

    Always<float> fireConeAngleCos;   // Fire_Cone_Angle (+0x1A4): stored value is cos(half the authored degrees); documented default 1.0
    std::optional<float> fireConeLength;  // Fire_Cone_Length (+0x1A8, i); default 0
    FireConeMetrics fireConeMetrics;        // Fire_Cone_Metrics (+0x1AC..)

    std::optional<uint8_t> shotsPerRound;  // Shots_Per_Round (+0x20C, i); default 1

    PlayerSpreadBlock playerWeaponSpread;      // PlayerWeaponSpread (+0x210)
    PlayerSpreadBlock playerAltWeaponSpread;    // PlayerAltWeaponSpread (+0x24C)
    NpcSpreadBlock npcWeaponSpread;               // NPCWeaponSpread (+0x288)
    NpcSpreadBlock npcAltWeaponSpread;              // NPCAltWeaponSpread (+0x2D4)

    std::optional<float> ragdollForceShoot;             // Ragdoll_Force_Shoot (+0x320, i); default 1.0
    std::optional<float> objectBulletHitImpulseMagnitude; // Object_Bullet_Hit_Impulse_Magnitude (+0x324, i); default 10.0
    RagdollInfo ragdollInfo;                                // Ragdoll_Info (+0x328..+0x344)

    ProjectileInfo projectileInfo;   // Projectile_Info (+0x348..+0x3AC)

    // melee_material_effects (+0x3B0..+0x433): the engine indexes 33 slots
    // by a physical-material name table (FUN_006F76F0) this spec never
    // enumerates (only referenced, section 1.6) - stored as raw
    // (material name, effect name) pairs, NOT resolved to a 33-slot array.
    std::vector<std::pair<std::string, std::optional<std::string>>> meleeMaterialEffects;

    std::optional<float> meleeDamageToAnchoredScaler;  // melee_damage_to_anchored_scaler (+0x434, i); default 0.2
    std::optional<float> vehicleDamageScale;             // vehicle_damage_scale (+0x438, i); default 1.0
    std::optional<float> playerVehicleDamageScale;         // player_vehicle_damage_scale (+0x43C, i); defaults to vehicleDamageScale

    std::optional<std::string> meleeAttackInfo;  // Melee_Attack_Info (+0x440): name of a weapon_melee_attacks Melee_Attack_Set (section 6); absent/unmatched -> engine built-in default

    WaterSprayInfo watersprayInfo;   // Waterspray_info (+0x444..+0x45C)
    ChargeReleaseInfo chargeReleaseInfo;  // Charge_Release_Info (+0x460..+0x484)
    VehicleWeaponBlock vehicleWeapon;       // Vehicle_Weapon (+0x488..+0x70F)
    CameraInfo cameraInfo;                    // Camera_Info (+0x710..+0x774)

    BurstFireInfo burstFireInfo;   // Burst_Fire_Info (+0x774..+0x778)
    std::optional<int32_t> npcDesiredBurstSize;  // NPC_Desired_Burst_Size (+0x77C, i): only meaningful when triggerType == 2 (automatic); documented default 3, else fixed 1

    std::optional<std::string> overrideBulletImpactEffect;      // Override_Bullet_Impact_Effect (+0x780)
    std::optional<std::string> altOverrideBulletImpactEffect;     // Alt_Override_Bullet_Impact_Effect (+0x784)
    std::optional<std::string> overrideBulletImpactEffectNpc;       // Override_Bullet_Impact_Effect_NPC (+0x788)
    std::optional<std::string> altOverrideBulletImpactEffectNpc;      // Alt_Override_Bullet_Impact_Effect_NPC (+0x78C)

    std::optional<std::string> penetratingEndPointExplosion;      // Penetrating_End_Point_Explosion (+0x790)
    std::optional<std::string> altPenetratingEndPointExplosion;     // Alt_Penetrating_End_Point_Explosion (+0x794)

    OverheatInfo overheatInfo;                     // Overheat_Info (+0x798..+0x7AC)
    std::vector<EffectSituationEntry> effectSituations;  // Effect_Situations (+0x7B0..+0x7CC), positional, capacity 4

    std::optional<int32_t> maxMeleeImpacts;  // Max_Melee_Impacts (+0x7D0, i); default -1

    // +0x7D4..+0x7E0 (Display_Name_Override/handle/Description_Override/
    // Bitmap_Override) are written only by weapon_upgrades patches, never
    // by this row reader (spec section 2.2 note) - not modelled here.

    std::optional<float> bloodDecalScale;   // Blood_Decal_Scale (+0x7E4, i); default 1.0
    std::optional<int32_t> bloodDecalDelay;   // Blood_Decal_Delay (+0x7E8, i); default -1
};

// Parses one `<Weapon>` row. `row` must be a `Weapon` element (case-insensitive)
// directly under `<Table>` (spec section 2.1/2.2).
Weapon ParseWeapon(const Node* row);

// Convenience: parses every `<Weapon>` row of a whole weapons.xtbl document,
// in file order (no Framework filtering - callers that need the base-game
// two-pass semantics of spec section 1.5 item 1 should filter on `framework`
// themselves).
std::vector<Weapon> ParseAllWeapons(const Document& doc);

// ---------------------------------------------------------------------------
// weapon_categories.xtbl (spec section 3)
// ---------------------------------------------------------------------------
struct WeaponCategoryRow {
    std::string name;         // Name, as authored (case folding for the enum match happens separately)
    int32_t categoryIndex = -1;  // Name matched against the 7 WPNCAT_* literals (enum); -1 if unmatched
};

// Parses one `<Categories>` row (the row element is literally "Categories", plural - spec section 3).
WeaponCategoryRow ParseWeaponCategory(const Node* row);
std::vector<WeaponCategoryRow> ParseAllWeaponCategories(const Document& doc);

}  // namespace sr3tables_weapons
