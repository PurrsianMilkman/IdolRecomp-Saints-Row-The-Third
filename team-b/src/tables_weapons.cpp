// Parse functions for sr3tables_weapons (spec-tables-weapons-combat.md).
// Built entirely on sr3xtbl's accessors (include/sr3xtbl/xtbl.h). See
// include/sr3tables_weapons/weapons.h for the shared conventions this file
// follows (Always<T> vs optional<T>, cross-table names kept as text, the
// one degrees->radians constant, what is deliberately not modelled).

#include "sr3tables_weapons/combat.h"
#include "sr3tables_weapons/weapons.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace sr3tables_weapons {

using namespace sr3xtbl;

namespace {

// ---------------------------------------------------------------------------
// Generic helpers
// ---------------------------------------------------------------------------
std::optional<std::string> OptText(const Node* node, std::string_view name) {
    const std::string* t = ChildText(node, name);
    if (!t) return std::nullopt;
    return *t;
}

template <size_t N>
int EnumIdx(const Node* node, std::string_view name, const std::string_view (&names)[N]) {
    return EnumIndex(FindChild(node, name), names, N);
}

template <size_t N>
uint32_t FlagBits(const Node* flagsNode, const std::string_view (&names)[N]) {
    return FlagMask(flagsNode, names, N);
}

Always<float> ReadDegAsRadAlways(const Node* node, std::string_view name) {
    Always<float> a = ReadFloatAlways(node, name);
    if (a.present) a.value *= kDegToRad;
    return a;
}
std::optional<float> ReadDegAsRadIfPresent(const Node* node, std::string_view name) {
    auto v = GetFloat(node, name);
    if (v) return *v * kDegToRad;
    return std::nullopt;
}
Always<float> ReadSecAsMsAlways(const Node* node, std::string_view name) {
    Always<float> a = ReadFloatAlways(node, name);
    if (a.present) a.value *= 1000.0f;
    return a;
}
std::optional<float> ReadSecAsMsIfPresent(const Node* node, std::string_view name) {
    auto v = GetFloat(node, name);
    if (v) return *v * 1000.0f;
    return std::nullopt;
}

// Constant_Effect/Condition (spec section 2.2): a 3-way match with default 0
// (NOT sr3xtbl::EnumIndex, which would give -1 for "no match").
int ConditionOf(const Node* ceNode) {
    const Node* cond = FindChild(ceNode, "Condition");
    if (!cond || !cond->text()) return 0;
    const std::string& t = *cond->text();
    if (NameEquals(t, "during melee swing")) return 1;
    if (NameEquals(t, "fine aim")) return 2;
    if (NameEquals(t, "weapon raised")) return 3;
    return 0;
}

}  // namespace

// ===========================================================================
// weapons.xtbl sub-block parsers (spec section 2.3), also reused verbatim by
// weapon_upgrades.xtbl (spec section 4.2: "parses the row's element with the
// SAME readers section 2 documents").
// ===========================================================================
namespace {

MeleeDamageOverrides ParseMeleeDamageOverrides(const Node* row) {
    MeleeDamageOverrides r;
    const Node* n = FindChild(row, "Melee_Damage_Overrides");
    r.present = (n != nullptr);
    r.npc = ReadFloatAlways(n, "NPC");
    r.player = ReadFloatAlways(n, "Player");
    r.online = ReadFloatAlways(n, "Online");
    return r;
}

TracerInfo ParseTracerInfo(const Node* row) {
    TracerInfo r;
    const Node* n = FindChild(row, "Tracer_Info");
    r.present = (n != nullptr);
    r.tracer = OptText(n, "Tracer");
    r.tracerNpc = OptText(n, "Tracer_NPC");
    r.altTracer = OptText(n, "Alt_Tracer");
    r.altTracerNpc = OptText(n, "Alt_Tracer_NPC");
    r.tracerFrequency = ReadUInt8Always(n, "Tracer_Frequency");
    return r;
}

std::vector<ConstantEffect> ParseConstantEffects(const Node* row) {
    std::vector<ConstantEffect> out;
    const Node* wrap = FindChild(row, "Constant_Effects");
    for (const Node* ce = FindChild(wrap, "Constant_Effect"); ce; ce = NextSibling(wrap, ce, "Constant_Effect")) {
        ConstantEffect e;
        e.effect = OptText(ce, "Effect");
        e.weaponPropPoint = OptText(ce, "Weapon_Prop_Point");
        e.condition = ConditionOf(ce);
        out.push_back(std::move(e));
    }
    return out;
}

AudioInfo ParseAudioInfo(const Node* row) {
    AudioInfo r;
    const Node* n = FindChild(row, "Audio");
    r.present = (n != nullptr);
    r.weaponModel = OptText(n, "Weapon_Model");
    r.soundbankName = OptText(n, "Soundbank_Name");
    r.stopOverrideEvent = OptText(n, "Stop_Override_Event");
    r.altFireStopOverrideEvent = OptText(n, "Alt_Fire_Stop_Override_Event");
    r.soundRadius = GetFloat(n, "Sound_Radius");
    r.looping = GetBool(n, "looping");
    r.altLooping = GetBool(n, "alt_looping");
    return r;
}

TargetLockon ParseTargetLockon(const Node* row) {
    TargetLockon r;
    const Node* n = FindChild(row, "Target_Lockon");
    r.present = (n != nullptr);
    r.lockonTimeMs = ReadInt32Always(n, "Lockon_Time_MS");
    r.lockingSizeMultiplier = ReadFloatAlways(n, "Locking_Size_Multiplier");
    r.lockedSizeMultiplier = ReadFloatAlways(n, "Locked_Size_Multiplier");
    r.beepTimingSlowest = ReadFloatAlways(n, "Beep_Timing_Slowest");
    r.beepTimingFastest = ReadFloatAlways(n, "Beep_Timing_Fastest");
    r.angleFromReticle = ReadFloatAlways(n, "Angle_From_Reticle");
    r.angleFromReticleLoseTarget = ReadFloatAlways(n, "Angle_From_Reticle_Lose_Target");
    r.lockingRotationFurthestAngleRadians = ReadDegAsRadAlways(n, "Locking_Rotation_Furthest_Angle");
    r.enemyAircraftOnly = HasFlag(FindChild(n, "flags"), "enemy aircraft only");
    return r;
}

TimeManagementBlock ParseTimeManagement(const Node* row, std::string_view wrapperName) {
    TimeManagementBlock r;
    const Node* n = FindChild(row, wrapperName);
    r.refireDelayMs = ReadUInt16Always(n, "Refire_Delay");
    const Node* npcRefire = FindChild(n, "npc_refire_delay");
    r.npcRefireDelayMin = ReadUInt16Always(npcRefire, "min");
    r.npcRefireDelayMax = ReadUInt16Always(npcRefire, "max");
    r.postDetonateDelay = ReadUInt16Always(n, "Post_Detonate_Delay");
    r.preDetonateDelay = ReadUInt16Always(n, "Pre_Detonate_Delay");
    {
        const std::string* t = ChildText(n, "Firecone_Ramp_In_Time");
        if (t) {
            r.firearmRampInTimeMs.value = static_cast<uint16_t>(ParseFloat(*t) * 1000.0f);
            r.firearmRampInTimeMs.present = true;
        }
    }
    {
        const std::string* t = ChildText(npcRefire, "npc_refire_type");
        r.npcRefireType = (t && !t->empty()) ? static_cast<uint8_t>((*t)[0] - '1') : uint8_t(0);
    }
    return r;
}

SpreadMovementMultipliersPlayer ParseSpreadMultPlayer(const Node* wrapper) {
    SpreadMovementMultipliersPlayer r;
    r.crouch = GetFloat(wrapper, "Movement_Multiplier_Crouch");
    r.walk = GetFloat(wrapper, "Movement_Multiplier_Walk");
    r.run = GetFloat(wrapper, "Movement_Multiplier_Run");
    r.sprint = GetFloat(wrapper, "Movement_Multiplier_Sprint");
    r.vehicle = GetFloat(wrapper, "Movement_Multiplier_Vehicle");
    r.fineAim = GetFloat(wrapper, "Movement_Multiplier_Fine_Aim");
    return r;
}
SpreadMovementMultipliersNpc ParseSpreadMultNpc(const Node* wrapper) {
    SpreadMovementMultipliersNpc r;
    r.crouch = GetFloat(wrapper, "Movement_Multiplier_Crouch");
    r.walk = GetFloat(wrapper, "Movement_Multiplier_Walk");
    r.run = GetFloat(wrapper, "Movement_Multiplier_Run");
    r.sprint = GetFloat(wrapper, "Movement_Multiplier_Sprint");
    r.vehicle = GetFloat(wrapper, "Movement_Multiplier_Vehicle");
    return r;
}

PlayerSpreadBlock ParsePlayerSpreadBlock(const Node* row, std::string_view wrapperName) {
    PlayerSpreadBlock r;
    const Node* n = FindChild(row, wrapperName);
    const Node* minmax = FindChild(n, "SpreadMinMax");
    r.spreadMinRadians = ReadDegAsRadAlways(minmax, "Player_Spread_Min");
    r.spreadMaxRadians = ReadDegAsRadAlways(minmax, "Player_Spread_Max");
    r.movementMultipliers = ParseSpreadMultPlayer(FindChild(n, "SpreadMovementMultipliers"));
    const Node* dyn = FindChild(n, "SpreadDynamics");
    r.toSpreadMax = ReadUInt8Always(dyn, "Player_To_Spread_Max");
    r.toSpreadMin = ReadUInt16Always(dyn, "Player_To_Spread_Min");
    r.dynamicMultipliers = ParseSpreadMultPlayer(FindChild(dyn, "SpreadDynamicMultipliers"));
    return r;
}

NpcSpreadBlock ParseNpcSpreadBlock(const Node* row, std::string_view wrapperName) {
    NpcSpreadBlock r;
    const Node* n = FindChild(row, wrapperName);
    const Node* minmax = FindChild(n, "SpreadMinMax");
    r.spreadMinRadians = ReadDegAsRadAlways(minmax, "NPC_Spread_Min");
    r.spreadMaxRadians = ReadDegAsRadAlways(minmax, "NPC_Spread_Max");
    r.movementMultipliers = ParseSpreadMultNpc(FindChild(n, "SpreadMovementMultipliers"));
    const Node* dyn = FindChild(n, "SpreadDynamics");
    r.toSpreadMax = ReadUInt8Always(dyn, "NPC_To_Spread_Max");
    r.toSpreadMin = ReadUInt16Always(dyn, "NPC_To_Spread_Min");
    r.dynamicMultipliers = ParseSpreadMultNpc(FindChild(dyn, "SpreadDynamicMultipliers"));
    const Node* tgt = FindChild(minmax, "SpreadTargetMovementMultipliers");
    r.targetMovementMultipliers.walk = GetFloat(tgt, "Target_Movement_Multiplier_Walk");
    r.targetMovementMultipliers.run = GetFloat(tgt, "Target_Movement_Multiplier_Run");
    r.targetMovementMultipliers.sprint = GetFloat(tgt, "Target_Movement_Multiplier_Sprint");
    r.targetMovementMultipliers.vehicle = GetFloat(tgt, "Target_Movement_Multiplier_Vehicle");
    return r;
}

FlatSpreadMetrics ParseFlatSpreadMetrics(const Node* row) {
    FlatSpreadMetrics r;
    const Node* n = FindChild(row, "Flat_Spread_Metrics");
    r.present = (n != nullptr);
    r.widthAngleRadians = ReadDegAsRadAlways(n, "Flat_Spread_Width_Angle");
    r.heightAngleRadians = ReadDegAsRadAlways(n, "Flat_Spread_Height_Angle");
    r.rotationRadians = ReadDegAsRadAlways(n, "Flat_Spread_Rotation");
    r.rotationPerShotRadians = ReadDegAsRadAlways(n, "Flat_Spread_Rotation_Per_Shot");
    return r;
}

FireConeMetrics ParseFireConeMetrics(const Node* row) {
    FireConeMetrics r;
    const Node* n = FindChild(row, "Fire_Cone_Metrics");
    r.present = (n != nullptr);
    const Node* mt = FindChild(n, "Metric_Type");
    const Node* mm = FindChild(mt, "Min_Max");
    r.isMinMaxForm = (mm != nullptr);
    if (mm) {
        r.nearRadius = GetFloat(mm, "Near_Radius");
        r.farRadius = GetFloat(mm, "Far_Radius");
    } else {
        const Node* ang = FindChild(mt, "Angle");
        r.angleDegrees = GetFloat(ang, "Angle");
    }
    return r;
}

RagdollInfo ParseRagdollInfo(const Node* row) {
    RagdollInfo r;
    const Node* n = FindChild(row, "Ragdoll_Info");
    r.chance = ReadFloatAlways(n, "Chance");
    r.deathVelocityHorizontal = ReadFloatAlways(n, "Death_Velocity_Horizontal");
    r.deathVelocityVertical = ReadFloatAlways(n, "Death_Velocity_Vertical");
    r.deathPointVelocity = ReadFloatAlways(n, "Death_Point_Velocity");
    r.deathAngularVelocityHorizontal = ReadFloatAlways(n, "Death_Angular_Velocity_Horizontal");
    r.deathAngularVelocityVertical = ReadFloatAlways(n, "Death_Angular_Velocity_Vertical");
    r.deathRangeMin = ReadFloatAlways(n, "Death_Range_Min");
    r.deathRangeMax = ReadFloatAlways(n, "Death_Range_Max");
    return r;
}

ProjectileInfo ParseProjectileInfo(const Node* row) {
    ProjectileInfo r;
    const Node* n = FindChild(row, "Projectile_Info");
    r.present = (n != nullptr);
    r.model = OptText(n, "Model");
    r.speed = ReadFloatAlways(n, "Speed");
    r.speedNpc = ReadFloatAlways(n, "Speed_NPC");
    r.postIgnitionSpeed = ReadFloatAlways(n, "Post_Ignition_Speed");
    r.postIgnitionSpeedNpc = ReadFloatAlways(n, "Post_Ignition_Speed_NPC");
    r.gravity = ReadFloatAlways(n, "Gravity");
    r.launchPitchChangeRadians = ReadDegAsRadAlways(n, "Launch_pitch_change_deg");
    r.attachedEffect = OptText(n, "Attached_Effect");
    r.creationEffect = OptText(n, "Creation_Effect");
    r.attachedEffectPropPoint = OptText(n, "Attached_Effect_Prop_Point");
    r.fuseTimeMs = ReadUInt16Always(n, "Fuse_Time");
    r.npcFuseTimeMs = ReadUInt16Always(n, "NPC_Fuse_Time");
    {
        const std::string* t = ChildText(n, "Fade_Out_Time");
        if (t) {
            float sec = ParseFloat(*t);
            r.fadeOutTimeMs.value = sec > 0.0f ? static_cast<uint16_t>(sec * 1000.0f) : uint16_t(0);
            r.fadeOutTimeMs.present = true;
        }
    }
    r.ignitionDelayMs = GetInt16(n, "Projectile_Ignition_Delay_MS");
    r.aiCanGuide = GetUInt16(n, "AI_Can_Guide");
    r.mass = ReadFloatAlways(n, "Mass");
    r.linearDamp = GetFloat(n, "Linear_Damp");
    r.angularDamp = GetFloat(n, "Angular_Damp");
    r.restitution = GetFloat(n, "Restitution");
    r.friction = GetFloat(n, "Friction");
    r.angularVelocity = ReadVec3Child(n, "Angular_Velocity");
    r.blowTireRadius = GetFloat(n, "Blow_Tire_Radius");
    r.ignitionEffect = OptText(n, "Ignition_Effect");
    r.sound = OptText(n, "Sound");
    r.foleyCollision = OptText(n, "FoleyCollision");
    uint32_t flags = FlagBits(FindChild(n, "Projectile_Flags"), kProjectileFlagNames);
    if (r.fuseTimeMs.present) flags |= 0x10000u;
    r.projectileFlags = flags;
    return r;
}

// [NOTE - spec-tables-weapons-combat.md 1.6 (`FUN_006F76F0` row) now lists the 33 names by cross-reference
// to spec-tables-environment.md 11.6 `bitmap_materials.xtbl` [CONFIRMED - disassembly]: slot 0..32, `not set` = 31,
// `_stricmp`, unknown -> 31. The earlier "never enumerated" wording below pre-dates that cross-reference. Still kept
// as raw text (label/note only, no behaviour change; resolution to a slot index is out of scope here).]
std::vector<std::pair<std::string, std::optional<std::string>>> ParseMeleeMaterialEffects(const Node* row) {
    std::vector<std::pair<std::string, std::optional<std::string>>> out;
    const Node* wrap = FindChild(row, "melee_material_effects");
    for (const Node* me = FindChild(wrap, "material_effect"); me; me = NextSibling(wrap, me, "material_effect")) {
        std::string material = OptText(me, "material").value_or(std::string());
        out.emplace_back(std::move(material), OptText(me, "effect"));
    }
    return out;
}

WaterSprayInfo ParseWaterSprayInfo(const Node* row) {
    WaterSprayInfo r;
    const Node* n = FindChild(row, "Waterspray_info");
    r.waterStreamForce = ReadFloatAlways(n, "Water_stream_force");
    r.refillRatePerSecond = ReadFloatAlways(n, "Refill_rate_per_Second");
    r.radiusExpansionRate = ReadFloatAlways(n, "Radius_Expansion_Rate");
    r.waterSprayEffectHash = NameHashOrZero(ChildText(n, "Waterspray_Effect"));
    r.pressureIncreaseRate = GetFloat(n, "Pressure_Increase_Rate");
    if (r.pressureIncreaseRate) {
        r.pressureDecreaseRate = GetFloat(n, "Pressure_Decrease_Rate");
        r.pressureRestoreTime = GetFloat(n, "Pressure_Restore_Time");
    }
    return r;
}

ChargeReleaseInfo ParseChargeReleaseInfo(const Node* row) {
    ChargeReleaseInfo r;
    const Node* n = FindChild(row, "Charge_Release_Info");
    if (auto sec = GetFloat(n, "Charge_Time_sec")) r.chargeTimeReciprocal = (*sec != 0.0f) ? (1.0f / *sec) : 0.0f;
    r.minChargePercent = ReadFloatAlways(n, "Min_Charge_Percent");
    r.chargeBase = ReadFloatAlways(n, "Charge_Base");
    r.autoRelease = ReadBoolAlways(n, "Auto_Release");
    r.minRange = ReadFloatAlways(n, "Min_Range");
    r.preChargeDelay = ReadFloatAlways(n, "Pre_Charge_Delay");
    if (auto sec = GetFloat(n, "Charge_Cooldown_Time")) r.chargeCooldownReciprocal = (*sec != 0.0f) ? (1.0f / *sec) : 0.0f;
    r.chargingCameraShake = OptText(n, "Charging_Camera_Shake");
    r.chargedCameraShake = OptText(n, "Charged_Camera_Shake");
    r.showHudOnCharge = HasFlag(FindChild(n, "Charge_Flags"), "Show HUD on charge");
    return r;
}

TurretPart ParseTurretPart(const Node* comp, std::string_view name) {
    TurretPart r;
    const Node* n = FindChild(comp, name);
    r.dontResetAngleWhenUnmanned = HasFlag(FindChild(n, "flags"), kTurretPartFlagNames[0]);
    r.minAngleRadians = ReadDegAsRadAlways(n, "Min_Angle");
    r.maxAngleRadians = ReadDegAsRadAlways(n, "Max_Angle");
    r.maxSpeedRadians = ReadDegAsRadAlways(n, "Max_Speed");
    r.dampAngleRadians = ReadDegAsRadAlways(n, "Damp_Angle");
    r.unmannedSpeedRadians = ReadDegAsRadAlways(n, "Unmanned_Speed");
    r.firingAngleSpeedRadians = ReadDegAsRadIfPresent(n, "Firing_Angle_Speed");
    Always<float> force = ReadFloatAlways(n, "Max_Force");
    if (force.present) force.value *= 4.448221683502197f;
    r.maxForceNewtons = force;
    return r;
}

VehicleWeaponComponent ParseVehicleWeaponComponent(const Node* weaponNode) {
    VehicleWeaponComponent r;
    r.uid = ReadUInt32Always(weaponNode, "UID");
    r.weaponClass = EnumIdx(weaponNode, "Weapon_Class", kWeaponClassNames);
    const Node* flags = FindChild(weaponNode, "flags");
    r.targetReticule = HasFlag(flags, kVehicleWeaponComponentFlagNames[0]);
    r.enablePhysics = HasFlag(flags, kVehicleWeaponComponentFlagNames[1]);
    r.muzzleExplosion = OptText(weaponNode, "Muzzle_Explosion");
    r.middleComponent = ParseTurretPart(weaponNode, "Middle_Component");
    r.topComponent = ParseTurretPart(weaponNode, "Top_Component");
    return r;
}

VehicleWeaponBlock ParseVehicleWeapon(const Node* row) {
    VehicleWeaponBlock r;
    const Node* n = FindChild(row, "Vehicle_Weapon");
    r.present = (n != nullptr);
    const Node* prim = FindChild(n, "Primary_Weapons");
    for (const Node* w = FindChild(prim, "Weapon"); w; w = NextSibling(prim, w, "Weapon"))
        r.primaryWeapons.push_back(ParseVehicleWeaponComponent(w));
    const Node* alt = FindChild(n, "Alt_Weapons");
    for (const Node* w = FindChild(alt, "Weapon"); w; w = NextSibling(alt, w, "Weapon"))
        r.altWeapons.push_back(ParseVehicleWeaponComponent(w));
    return r;
}

CameraShakeWithIntensity ParseShake(const Node* n, std::string_view nameElem, std::string_view intensityElem) {
    CameraShakeWithIntensity r;
    r.shakeName = OptText(n, nameElem);
    r.intensity = ReadFloatAlways(n, intensityElem);
    return r;
}

constexpr std::string_view kZoomTypeNames[2] = {"progressive", "non-progressive"};

CameraInfo ParseCameraInfo(const Node* row) {
    CameraInfo r;
    const Node* n = FindChild(row, "Camera_Info");
    r.primaryFire = ParseShake(n, "Primary_Fire_Camera_Shake", "Primary_Fire_Camera_Shake_Intensity");
    r.primaryFineAim = ParseShake(n, "Primary_Fine_Aim_Camera_Shake", "Primary_Fire_Fine_Aim_Camera_Shake_Intensity");
    r.secondaryFire = ParseShake(n, "Secondary_Fire_Camera_Shake", "Secondary_Fire_Camera_Shake_Intensity");
    r.secondaryFireFineAim = ParseShake(n, "Secondary_Fire_Fine_Aim_Camera_Shake", "Secondary_Fire_Fine_Aim_Camera_Shake_Intensity");
    r.meleeHard = ParseShake(n, "Melee_Hard_Camera_Shake", "Melee_Hard_Camera_Shake_Intensity");
    r.meleeSoft = ParseShake(n, "Melee_Soft_Camera_Shake", "Melee_Soft_Camera_Shake_Intensity");
    r.playerHit = ParseShake(n, "Player_Hit_Camera_Shake", "Player_Hit_Camera_Shake_Intensity");
    r.primaryRecoilMultiplier = GetFloat(n, "Primary_Recoil_Multiplier");
    r.primaryFineAimRecoilMultiplier = GetFloat(n, "Primary_Fine_Aim_Recoil_Multiplier");
    r.primaryRecoilDelayMs = ReadInt32Always(n, "Primary_Recoil_Delay_ms");
    r.primaryRecoilRamped = ReadBoolAlways(n, "Primary_Recoil_Ramped");
    r.secondaryRecoilMultiplier = GetFloat(n, "Secondary_Recoil_Multiplier");
    r.secondaryFineAimRecoilMultiplier = GetFloat(n, "Secondary_Fine_Aim_Recoil_Multiplier");
    if (const Node* zt = FindChild(n, "Zoom_Type"); zt && zt->text()) r.zoomType = EnumIndex(zt, kZoomTypeNames, 2);
    r.minimumFov = GetFloat(n, "Minimum_FOV");
    r.maximumFov = GetFloat(n, "Maximum_FOV");
    r.zoomSteps = GetFloat(n, "Zoom_Steps");
    r.fovRate = GetFloat(n, "FOV_Rate");
    return r;
}

OverheatInfo ParseOverheatInfo(const Node* row) {
    OverheatInfo r;
    const Node* n = FindChild(row, "Overheat_Info");
    r.percentIncreasePerShot = ReadFloatAlways(n, "Percent_Increase_Per_Shot");
    r.percentDecreasePerSecond = ReadFloatAlways(n, "Percent_Decrease_Per_Second");
    r.percentDecreasePerReload = ReadFloatAlways(n, "Percent_Decrease_Per_Reload");
    r.percentDecreasePerSecondOverheated = ReadFloatAlways(n, "Percent_Decrease_Per_Second_Overheated");
    r.percentDecreasePerReloadOverheated = ReadFloatAlways(n, "Percent_Decrease_Per_Reload_Overheated");
    r.overheatFlags = FlagBits(FindChild(n, "Overheat_Flags"), kOverheatFlagNames);
    return r;
}

std::vector<EffectSituationEntry> ParseEffectSituations(const Node* row) {
    std::vector<EffectSituationEntry> out;
    const Node* wrap = FindChild(row, "Effect_Situations");
    if (!wrap) return out;
    const auto& kids = wrap->children();
    for (size_t i = 0; i < kids.size() && out.size() < 4; ++i) {
        const Node* c = kids[i];
        EffectSituationEntry e;
        e.situation = EnumIdx(c, "Situation", kEffectSituationNames);
        e.effect = OptText(c, "Effect");
        out.push_back(std::move(e));
    }
    return out;
}

BurstFireInfo ParseBurstFireInfo(const Node* row) {
    BurstFireInfo r;
    const Node* n = FindChild(row, "Burst_Fire_Info");
    r.shots = ReadInt32Always(n, "Shots");
    r.burstDelayMs = ReadInt32Always(n, "Burst_Delay_ms");
    return r;
}

}  // namespace

// ===========================================================================
// 2. weapons.xtbl
// ===========================================================================
Weapon ParseWeapon(const Node* row) {
    Weapon w;
    w.name = OptText(row, "Name").value_or(std::string());
    w.framework = OptText(row, "Framework");
    w.infoSlotIndex = GetUInt32(row, "Info_Slot_Index");
    w.baseVersion = OptText(row, "Base_Version");
    w.isDlc = GetBool(row, "Is_DLC");

    const Node* flagsNode = FindChild(row, "Flags");
    w.flagsA = FlagBits(flagsNode, kFlagsAWordNames);
    w.flagsB = FlagBits(flagsNode, kFlagsBWordNames);
    w.flagsC = FlagBits(flagsNode, kFlagsCWordNames);
    if (NameEquals(w.name, "sp_gat01_w")) w.flagsA |= 0x8u;  // name-based special case, spec section 2.4

    w.specialCaseType = EnumIdx(row, "Special_Case_Type", kSpecialCaseTypeNames);
    w.meleeDamageOverrides = ParseMeleeDamageOverrides(row);

    w.animationGroup = OptText(row, "Animation_Group");
    w.fineAimAnimationGroup = OptText(row, "Fine_Aim_Animation_Group");
    w.reloadAnimationGroup = OptText(row, "Reload_Animation_Group");

    w.reloadOverrideTimeSec = GetFloat(row, "Reload_override_time_sec");
    w.warmupDelay = GetUInt16(row, "Warmup_Delay");
    w.cooldownDelay = GetUInt16(row, "Cooldown_Delay");

    w.grenadeType = EnumIdx(row, "Grenade_Type", kGrenadeTypeNames);
    w.strafeAngles = OptText(row, "Strafe_Angles");
    w.weaponClass = EnumIdx(row, "Weapon_Class", kWeaponClassNames);
    if (const Node* catNode = FindChild(row, "Category"); catNode && catNode->text())
        w.category = EnumIndex(catNode, kWeaponCategoryNames, 7);
    {
        int idx = EnumIdx(row, "Inv_Slot", kInvSlotNames);
        w.invSlot = (idx < 0) ? uint8_t(0xFF) : static_cast<uint8_t>(idx);
    }

    w.muzzleEffect = OptText(row, "Muzzle_Effect");
    w.altMuzzleEffect = OptText(row, "Alt_Muzzle_Effect");
    w.meleeEffect = OptText(row, "Melee_Effect");
    w.fireConeImpactEffect = OptText(row, "Fire_Cone_Impact_Effect");
    w.offhandWeaponMeshHash = NameHashOrZero(ChildText(row, "Offhand_Weapon_Mesh"));

    w.tracerInfo = ParseTracerInfo(row);
    w.constantEffects = ParseConstantEffects(row);

    w.brass = OptText(row, "Brass");
    w.diversionKillMultiplier = GetFloat(row, "Diversion_Kill_Multiplier");

    w.audio = ParseAudioInfo(row);
    w.targetLockon = ParseTargetLockon(row);

    w.triggerType = EnumIdx(row, "Trigger_Type", kTriggerTypeNames);
    w.altTriggerType = EnumIdx(row, "Alt_Trigger_Type", kTriggerTypeNames);

    w.ammo = OptText(row, "Ammo");
    w.altAmmo = OptText(row, "Alt_Ammo");
    w.magazineSize = ReadUInt16Always(row, "Magazine_Size");
    w.ammoPerShot = ReadUInt16Always(row, "Ammo_per_Shot");
    w.ammoRegeneration = ReadFloatAlways(row, "Ammo_Regeneration");
    w.rangeMax = ReadFloatAlways(row, "Range_Max");
    w.aiIdealRangeMin = ReadFloatAlways(row, "AI_Ideal_Range_Min");
    w.aiIdealRangeMax = ReadFloatAlways(row, "AI_Ideal_Range_Max");

    w.npcAimDrift = OptText(row, "NPC_Aim_Drift");

    w.timeManagement = ParseTimeManagement(row, "Time_Management");
    w.altTimeManagement = ParseTimeManagement(row, "Alt_Time_Management");

    {
        const Node* dmax = FindChild(row, "Damage_Max");
        w.damageMaxNpc = ReadFloatAlways(dmax, "NPC_Damage");
        w.damageMaxPlayer = ReadFloatAlways(dmax, "Player_Damage");
    }
    {
        const Node* dmin = FindChild(row, "Damage_Min");
        w.damageMinPresent = (dmin != nullptr);
        if (dmin) {
            w.damageMinNpc = ReadFloatAlways(dmin, "NPC_Damage");
            w.damageMinPlayer = ReadFloatAlways(dmin, "Player_Damage");
        }
    }

    w.riotShieldDamageMultiplier = ReadFloatAlways(row, "Riot_Shield_Damage_Multiplier");

    w.explosion = OptText(row, "Explosion");
    w.altExplosion = OptText(row, "Alt_Explosion");
    w.npcExplosion = OptText(row, "NPC_Explosion");
    w.npcAltExplosion = OptText(row, "NPC_Alt_Explosion");
    w.underwaterExplosion = OptText(row, "Underwater_Explosion");

    w.damageMaxDist = GetFloat(row, "Damage_Max_Dist");
    w.damageMinDist = GetFloat(row, "Damage_Min_Dist");
    w.operatorDamageMultiplier = GetFloat(row, "Operator_Damage_Multiplier");

    if (w.specialCaseType == kSpecialCaseWieldableProp) {
        w.wieldablePropDeathVfx = OptText(row, "Wieldable_Prop_Death_VFX");
        w.wieldablePropHitsAllowed = GetUInt16(row, "Wieldable_Prop_Hits_Allowed");
    }

    w.flatSpreadMetrics = ParseFlatSpreadMetrics(row);

    {
        // [HIGH CONFIDENCE - spec-tables-weapons-combat.md 2.2 (`+0x1A4` Fire_Cone_Angle): "the stored
        // value is cos(1/2 x angle) (HIGH CONFIDENCE that the transcendental is cosine)"; also
        // 2.3 Fire_Cone_Metrics `Angle` form. Same row: [OPEN] the reader flavour (always / if
        // present) and which of the two `+0x1A4` writes wins are not stated. Conversion kept
        // as-is; not CONFIRMED.]
        Always<float> angle = ReadFloatAlways(row, "Fire_Cone_Angle");
        if (angle.present) {
            w.fireConeAngleCos.value = std::cos(angle.value * 0.5f * kDegToRad);
            w.fireConeAngleCos.present = true;
        }
    }
    w.fireConeLength = GetFloat(row, "Fire_Cone_Length");
    w.fireConeMetrics = ParseFireConeMetrics(row);

    w.shotsPerRound = GetUInt8(row, "Shots_Per_Round");

    w.playerWeaponSpread = ParsePlayerSpreadBlock(row, "PlayerWeaponSpread");
    w.playerAltWeaponSpread = ParsePlayerSpreadBlock(row, "PlayerAltWeaponSpread");
    w.npcWeaponSpread = ParseNpcSpreadBlock(row, "NPCWeaponSpread");
    w.npcAltWeaponSpread = ParseNpcSpreadBlock(row, "NPCAltWeaponSpread");

    w.ragdollForceShoot = GetFloat(row, "Ragdoll_Force_Shoot");
    w.objectBulletHitImpulseMagnitude = GetFloat(row, "Object_Bullet_Hit_Impulse_Magnitude");
    w.ragdollInfo = ParseRagdollInfo(row);

    w.projectileInfo = ParseProjectileInfo(row);
    w.meleeMaterialEffects = ParseMeleeMaterialEffects(row);

    w.meleeDamageToAnchoredScaler = GetFloat(row, "melee_damage_to_anchored_scaler");
    w.vehicleDamageScale = GetFloat(row, "vehicle_damage_scale");
    w.playerVehicleDamageScale = GetFloat(row, "player_vehicle_damage_scale");

    w.meleeAttackInfo = OptText(row, "Melee_Attack_Info");

    w.watersprayInfo = ParseWaterSprayInfo(row);
    w.chargeReleaseInfo = ParseChargeReleaseInfo(row);
    w.vehicleWeapon = ParseVehicleWeapon(row);
    w.cameraInfo = ParseCameraInfo(row);

    w.burstFireInfo = ParseBurstFireInfo(row);
    if (w.triggerType == 2) w.npcDesiredBurstSize = GetInt32(row, "NPC_Desired_Burst_Size");

    w.overrideBulletImpactEffect = OptText(row, "Override_Bullet_Impact_Effect");
    w.altOverrideBulletImpactEffect = OptText(row, "Alt_Override_Bullet_Impact_Effect");
    w.overrideBulletImpactEffectNpc = OptText(row, "Override_Bullet_Impact_Effect_NPC");
    w.altOverrideBulletImpactEffectNpc = OptText(row, "Alt_Override_Bullet_Impact_Effect_NPC");

    w.penetratingEndPointExplosion = OptText(row, "Penetrating_End_Point_Explosion");
    w.altPenetratingEndPointExplosion = OptText(row, "Alt_Penetrating_End_Point_Explosion");

    w.overheatInfo = ParseOverheatInfo(row);
    w.effectSituations = ParseEffectSituations(row);

    w.maxMeleeImpacts = GetInt32(row, "Max_Melee_Impacts");

    w.bloodDecalScale = GetFloat(row, "Blood_Decal_Scale");
    w.bloodDecalDelay = GetInt32(row, "Blood_Decal_Delay");

    return w;
}

std::vector<Weapon> ParseAllWeapons(const Document& doc) {
    std::vector<Weapon> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Weapon"); row; row = NextSibling(table, row, "Weapon")) out.push_back(ParseWeapon(row));
    return out;
}

// ===========================================================================
// 3. weapon_categories.xtbl
// ===========================================================================
WeaponCategoryRow ParseWeaponCategory(const Node* row) {
    WeaponCategoryRow r;
    r.name = OptText(row, "Name").value_or(std::string());
    r.categoryIndex = EnumIdx(row, "Name", kWeaponCategoryNames);
    return r;
}
std::vector<WeaponCategoryRow> ParseAllWeaponCategories(const Document& doc) {
    std::vector<WeaponCategoryRow> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Categories"); row; row = NextSibling(table, row, "Categories"))
        out.push_back(ParseWeaponCategory(row));
    return out;
}

// ===========================================================================
// 4. weapon_upgrades.xtbl
// ===========================================================================
namespace {

OperationMode ReadOm(const Node* row, std::string_view elemName) {
    std::string omName(elemName);
    omName += "_OM";
    const std::string* t = ChildText(row, omName);
    if (!t) return OperationMode::Replace;
    if (NameEquals(*t, "OM_ADDITIVE")) return OperationMode::Additive;
    if (NameEquals(*t, "OM_MULTIPLICATIVE")) return OperationMode::Multiplicative;
    if (NameEquals(*t, "OM_REMOVAL")) return OperationMode::Removal;
    return OperationMode::Replace;
}

template <class T, class F>
WeaponOverride<T> MakeOverride(const Node* row, std::string_view elemName, F&& valueFn) {
    WeaponOverride<T> ov;
    ov.value = valueFn();
    ov.mode = ReadOm(row, elemName);
    return ov;
}

}  // namespace

WeaponUpgrade ParseWeaponUpgrade(const Node* row) {
    WeaponUpgrade u;
    u.name = OptText(row, "Name").value_or(std::string());
    const Node* editor = FindChild(row, "_Editor");
    u.editorCategory = OptText(editor, "Category");
    if (u.editorCategory) {
        const std::string& cat = *u.editorCategory;
        size_t pos = cat.find_last_of(':');
        u.targetWeaponName = (pos == std::string::npos) ? cat : cat.substr(pos + 1);
    }
    u.upgradeDescription = OptText(row, "upgrade_description");
    u.upgradePrice = GetUInt32(row, "upgrade_price");

    auto text = [&](std::string_view elem) -> std::optional<WeaponOverride<std::string>> {
        auto t = OptText(row, elem);
        if (!t) return std::nullopt;
        return MakeOverride<std::string>(row, elem, [&] { return *t; });
    };
    auto flt = [&](std::string_view elem) -> std::optional<WeaponOverride<float>> {
        auto v = GetFloat(row, elem);
        if (!v) return std::nullopt;
        return MakeOverride<float>(row, elem, [&] { return *v; });
    };

    u.displayNameOverride = text("Display_Name_Override");
    u.descriptionOverride = text("Description_Override");
    u.bitmapOverride = text("Bitmap_Override");
    u.objectItemOverride = text("Object_Item_Override");

    u.animationGroup = text("Animation_Group");
    u.fineAimAnimationGroup = text("Fine_Aim_Animation_Group");
    u.reloadAnimationGroup = text("Reload_Animation_Group");
    u.strafeAngles = text("Strafe_Angles");
    u.muzzleEffect = text("Muzzle_Effect");
    u.altMuzzleEffect = text("Alt_Muzzle_Effect");
    u.meleeEffect = text("Melee_Effect");
    u.fireConeImpactEffect = text("Fire_Cone_Impact_Effect");

    if (FindChild(row, "Tracer_Info")) u.tracerInfo = MakeOverride<TracerInfo>(row, "Tracer_Info", [&] { return ParseTracerInfo(row); });
    if (FindChild(row, "Constant_Effects"))
        u.constantEffects = MakeOverride<std::vector<ConstantEffect>>(row, "Constant_Effects", [&] { return ParseConstantEffects(row); });
    u.brass = text("Brass");
    u.soundRadius = flt("Sound_Radius");
    if (FindChild(row, "Target_Lockon"))
        u.targetLockon = MakeOverride<TargetLockon>(row, "Target_Lockon", [&] { return ParseTargetLockon(row); });

    if (const Node* n = FindChild(row, "Trigger_Type"); n && n->text())
        u.triggerType = MakeOverride<int32_t>(row, "Trigger_Type", [&] { return EnumIndex(n, kTriggerTypeNames, 4); });
    if (const Node* n = FindChild(row, "Alt_Trigger_Type"); n && n->text())
        u.altTriggerType = MakeOverride<int32_t>(row, "Alt_Trigger_Type", [&] { return EnumIndex(n, kTriggerTypeNames, 4); });
    u.ammo = text("Ammo");
    u.altAmmo = text("Alt_Ammo");
    if (auto v = GetUInt16(row, "Magazine_Size")) u.magazineSize = MakeOverride<uint16_t>(row, "Magazine_Size", [&] { return *v; });
    u.rangeMax = flt("Range_Max");
    u.npcAimDrift = text("NPC_Aim_Drift");

    if (FindChild(row, "Time_Management"))
        u.timeManagement = MakeOverride<TimeManagementBlock>(row, "Time_Management", [&] { return ParseTimeManagement(row, "Time_Management"); });
    if (FindChild(row, "Alt_Time_Management"))
        u.altTimeManagement =
            MakeOverride<TimeManagementBlock>(row, "Alt_Time_Management", [&] { return ParseTimeManagement(row, "Alt_Time_Management"); });
    if (const Node* n = FindChild(row, "Damage_Max"))
        u.damageMax = MakeOverride<DamageMaxMinOverride>(row, "Damage_Max", [&] {
            DamageMaxMinOverride d;
            d.npcDamage = ReadFloatAlways(n, "NPC_Damage");
            d.playerDamage = ReadFloatAlways(n, "Player_Damage");
            return d;
        });
    if (const Node* n = FindChild(row, "Damage_Min"))
        u.damageMin = MakeOverride<DamageMaxMinOverride>(row, "Damage_Min", [&] {
            DamageMaxMinOverride d;
            d.npcDamage = ReadFloatAlways(n, "NPC_Damage");
            d.playerDamage = ReadFloatAlways(n, "Player_Damage");
            return d;
        });
    u.explosion = text("Explosion");
    u.underwaterExplosion = text("Underwater_Explosion");
    u.damageMaxDist = flt("Damage_Max_Dist");
    u.damageMinDist = flt("Damage_Min_Dist");

    if (FindChild(row, "Flat_Spread_Metrics"))
        u.flatSpreadMetrics = MakeOverride<FlatSpreadMetrics>(row, "Flat_Spread_Metrics", [&] { return ParseFlatSpreadMetrics(row); });
    u.fireConeAngleDegrees = flt("Fire_Cone_Angle");
    u.fireConeLength = flt("Fire_Cone_Length");
    if (FindChild(row, "Fire_Cone_Metrics"))
        u.fireConeMetrics = MakeOverride<FireConeMetrics>(row, "Fire_Cone_Metrics", [&] { return ParseFireConeMetrics(row); });
    if (auto v = GetUInt8(row, "Shots_Per_Round")) u.shotsPerRound = MakeOverride<uint8_t>(row, "Shots_Per_Round", [&] { return *v; });

    if (FindChild(row, "PlayerWeaponSpread"))
        u.playerWeaponSpread = MakeOverride<PlayerSpreadBlock>(row, "PlayerWeaponSpread", [&] { return ParsePlayerSpreadBlock(row, "PlayerWeaponSpread"); });
    if (FindChild(row, "PlayerAltWeaponSpread"))
        u.playerAltWeaponSpread =
            MakeOverride<PlayerSpreadBlock>(row, "PlayerAltWeaponSpread", [&] { return ParsePlayerSpreadBlock(row, "PlayerAltWeaponSpread"); });
    if (FindChild(row, "NPCWeaponSpread"))
        u.npcWeaponSpread = MakeOverride<NpcSpreadBlock>(row, "NPCWeaponSpread", [&] { return ParseNpcSpreadBlock(row, "NPCWeaponSpread"); });
    if (FindChild(row, "NPCAltWeaponSpread"))
        u.npcAltWeaponSpread = MakeOverride<NpcSpreadBlock>(row, "NPCAltWeaponSpread", [&] { return ParseNpcSpreadBlock(row, "NPCAltWeaponSpread"); });

    u.ragdollForceShoot = flt("Ragdoll_Force_Shoot");
    u.objectBulletHitImpulseMagnitude = flt("Object_Bullet_Hit_Impulse_Magnitude");
    if (FindChild(row, "Ragdoll_Info")) u.ragdollInfo = MakeOverride<RagdollInfo>(row, "Ragdoll_Info", [&] { return ParseRagdollInfo(row); });
    if (FindChild(row, "Projectile_Info"))
        u.projectileInfo = MakeOverride<ProjectileInfo>(row, "Projectile_Info", [&] { return ParseProjectileInfo(row); });
    if (FindChild(row, "melee_material_effects"))
        u.meleeMaterialEffects = MakeOverride<std::vector<std::pair<std::string, std::optional<std::string>>>>(
            row, "melee_material_effects", [&] { return ParseMeleeMaterialEffects(row); });

    u.meleeDamageToAnchoredScaler = flt("melee_damage_to_anchored_scaler");
    u.vehicleDamageScale = flt("vehicle_damage_scale");
    u.playerVehicleDamageScale = flt("player_vehicle_damage_scale");
    u.meleeAttackInfo = text("Melee_Attack_Info");
    if (FindChild(row, "Charge_Release_Info"))
        u.chargeReleaseInfo = MakeOverride<ChargeReleaseInfo>(row, "Charge_Release_Info", [&] { return ParseChargeReleaseInfo(row); });

    if (FindChild(row, "Camera_Info")) u.cameraInfo = MakeOverride<CameraInfo>(row, "Camera_Info", [&] { return ParseCameraInfo(row); });
    if (FindChild(row, "Burst_Fire_Info"))
        u.burstFireInfo = MakeOverride<BurstFireInfo>(row, "Burst_Fire_Info", [&] { return ParseBurstFireInfo(row); });
    u.overrideBulletImpactEffect = text("Override_Bullet_Impact_Effect");
    u.altOverrideBulletImpactEffect = text("Alt_Override_Bullet_Impact_Effect");
    u.penetratingEndPointExplosion = text("Penetrating_End_Point_Explosion");
    u.altPenetratingEndPointExplosion = text("Alt_Penetrating_End_Point_Explosion");
    if (FindChild(row, "Overheat_Info")) u.overheatInfo = MakeOverride<OverheatInfo>(row, "Overheat_Info", [&] { return ParseOverheatInfo(row); });
    if (FindChild(row, "Effect_Situations"))
        u.effectSituations =
            MakeOverride<std::vector<EffectSituationEntry>>(row, "Effect_Situations", [&] { return ParseEffectSituations(row); });

    if (FindChild(row, "Melee_Damage_Overrides"))
        u.meleeDamageOverrides = MakeOverride<MeleeDamageOverrides>(row, "Melee_Damage_Overrides", [&] { return ParseMeleeDamageOverrides(row); });
    if (const Node* n = FindChild(row, "Flags")) {
        FlagsOverride fo;
        fo.flagsA = FlagBits(n, kFlagsAWordNames);
        fo.flagsB = FlagBits(n, kFlagsBWordNames);
        fo.flagsC = FlagBits(n, kFlagsCWordNames);
        u.flags = MakeOverride<FlagsOverride>(row, "Flags", [&] { return fo; });
    }

    return u;
}

std::vector<WeaponUpgrade> ParseAllWeaponUpgrades(const Document& doc) {
    std::vector<WeaponUpgrade> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Weapon_Upgrade"); row; row = NextSibling(table, row, "Weapon_Upgrade"))
        out.push_back(ParseWeaponUpgrade(row));
    return out;
}

// ===========================================================================
// 5. ammo.xtbl
// ===========================================================================
AmmoRecord ParseAmmo(const Node* row) {
    AmmoRecord r;
    r.name = OptText(row, "Name").value_or(std::string());
    r.flags = FlagBits(FindChild(row, "Flags"), kAmmoFlagNames);
    r.maxInReserve = ReadUInt32Always(row, "Max_in_Reserve");
    {
        int idx = EnumIdx(row, "Inv_Slot", kInvSlotNames);
        r.invSlot = (idx < 0) ? uint8_t(0xFF) : static_cast<uint8_t>(idx);
    }
    const Node* ua = FindChild(row, "Upgradable_Ammo");
    r.upgradableAmmoWeapon = OptText(ua, "Weapon");
    r.upgradableAmmoLevel2 = ReadUInt32Always(ua, "Level_2_Ammo");
    r.upgradableAmmoLevel3 = ReadUInt32Always(ua, "Level_3_Ammo");
    r.upgradableAmmoLevel4 = ReadUInt32Always(ua, "Level_4_Ammo");
    const Node* store = FindChild(row, "store");
    r.storeCost = ReadUInt32Always(store, "cost");
    r.storeClipSize = ReadUInt32Always(store, "clip_size");
    return r;
}
std::vector<AmmoRecord> ParseAllAmmo(const Document& doc) {
    std::vector<AmmoRecord> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Ammo"); row; row = NextSibling(table, row, "Ammo")) out.push_back(ParseAmmo(row));
    return out;
}

// ===========================================================================
// 6. weapon_melee_attacks.xtbl
// ===========================================================================
MeleeAttackSet ParseMeleeAttackSet(const Node* row) {
    MeleeAttackSet r;
    r.name = OptText(row, "Name").value_or(std::string());
    r.standingPrimary = OptText(row, "StandingPrimary");
    r.standingSecondary = OptText(row, "StandingSecondary");
    r.movingPrimary = OptText(row, "MovingPrimary");
    r.movingSecondary = OptText(row, "MovingSecondary");
    r.crouching = OptText(row, "Crouching");
    r.crouchMoving = OptText(row, "CrouchMoving");
    r.proneAttackPrimary = OptText(row, "ProneAttackPrimary");
    r.proneAttackSecondary = OptText(row, "ProneAttackSecondary");
    r.hardPrimary = OptText(row, "Hard_Primary");
    r.hardSecondary = OptText(row, "Hard_Secondary");
    r.nonFancyPrimary = OptText(row, "Non_Fancy_Primary");
    r.nonFancySecondary = OptText(row, "Non_Fancy_Secondary");
    r.standingSynced = OptText(row, "StandingSynced");
    r.movingSynced = OptText(row, "MovingSynced");
    r.targetSearchRange = ReadFloatAlways(row, "Target_Search_Range");
    return r;
}
std::vector<MeleeAttackSet> ParseAllMeleeAttackSets(const Document& doc) {
    std::vector<MeleeAttackSet> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Melee_Attack_Set"); row; row = NextSibling(table, row, "Melee_Attack_Set"))
        out.push_back(ParseMeleeAttackSet(row));
    return out;
}

// ===========================================================================
// 7.1 melee.xtbl
// ===========================================================================
namespace {

constexpr std::string_view kMeleeProcessingFlagNames[25] = {
    "anim state attack", "", "", "", "", "two-handed attack", "dont_play_weapon_sound", "unblockable", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "allow synced incapacitated victim", "", "attacker should not flinch",
};
constexpr std::string_view kMeleeAttackIsForFlagNames[22] = {
    "", "Hard hit (LT)", "Victim is Blocking", "Victim is Crouched", "Victim is Prone/Ragdolled", "", "", "", "", "", "", "",
    "Attacker is Player Only", "Attacker is Homie Only", "Attacker is Brute", "Attacker is Killbane", "Attacker is Avatar",
    "Attacker is Running", "Attacker is Walking", "Attacker is Sprinting", "Requires Previous Move Hit", "Victim is Not Ally",
};
constexpr std::string_view kMeleeAttackResultsFlagNames[12] = {
    "", "", "", "", "", "", "", "", "Kill victim", "Ragdoll Victim", "Ragdoll Victim from behind", "Disarm Victim",
};

std::vector<MeleeActiveAttackInfo> ParseActiveAttackInfos(const Node* row) {
    std::vector<MeleeActiveAttackInfo> out;
    const Node* wrap = FindChild(row, "Active_Attack_Infos");
    if (!wrap) return out;
    for (const Node* c : wrap->children()) {
        if (out.size() >= 6) break;
        MeleeActiveAttackInfo e;
        e.qteHudText = OptText(c, "QTE_HUD");
        e.successExtraDamage = ReadFloatAlways(c, "Success_Extra_Damage");
        e.successEffect = OptText(c, "Success_Effect");
        e.failEffect = OptText(c, "Fail_Effect");
        e.effectTag = OptText(c, "Effect_Tag");
        e.effectOnVictim = ReadBoolAlways(c, "Effect_on_Victim");
        out.push_back(std::move(e));
    }
    return out;
}

}  // namespace

MeleeMove ParseMeleeMove(const Node* row) {
    MeleeMove r;
    r.name = OptText(row, "Name").value_or(std::string());
    r.attackAnim = OptText(row, "AttackAnim");
    r.syncedMove = OptText(row, "SyncedMove");
    const Node* impact = FindChild(row, "Impact");
    r.impactFx = OptText(impact, "ImpactFX");
    r.impactHumanEffect = OptText(impact, "Impact_Human_Effect");
    r.impactDir = ReadVec3Child(impact, "ImpactDir");
    r.impactForce = ReadFloatAlways(impact, "ImpactForce");
    const Node* dd = FindChild(row, "DefaultDamage");
    r.defaultDamageNpc = ReadFloatAlways(dd, "NPCDamage");
    r.defaultDamagePlayer = ReadFloatAlways(dd, "PlayerDamage");
    r.defaultDamageOnline = ReadFloatAlways(dd, "OnlineDamage");
    r.ragdollGetupTimeMs = GetInt32(row, "Ragdoll_Getup_Time_ms");
    r.explosionHash = NameHashOrZero(ChildText(row, "Explosion"));
    r.activeAttackInfos = ParseActiveAttackInfos(row);
    {
        const Node* alNode = impact ? FindChild(impact, "AttackLimb") : nullptr;
        r.attackLimb = alNode ? EnumIndex(alNode, kAttackLimbNames, 6) : 0;
        const Node* alnpcNode = impact ? FindChild(impact, "AttackLimbNPC") : nullptr;
        r.attackLimbNpc = alnpcNode ? EnumIndex(alnpcNode, kAttackLimbNames, 6) : r.attackLimb;
    }
    uint32_t flagsF8 = FlagBits(FindChild(row, "Processing_flags"), kMeleeProcessingFlagNames) |
                        FlagBits(FindChild(row, "Attack_is_for"), kMeleeAttackIsForFlagNames) |
                        FlagBits(FindChild(row, "Attack_Results"), kMeleeAttackResultsFlagNames);
    r.testicularAssault = GetBool(row, "Testicular_Assault").value_or(false);
    if (r.testicularAssault) flagsF8 |= 0x800000u;
    if (!r.activeAttackInfos.empty()) flagsF8 |= 0x2000000u;
    r.flagsF8 = flagsF8;
    r.impactReactionTimeMs = GetInt32(impact, "Impact_reaction_time");
    r.impactReactionReverseSpeed = GetFloat(impact, "Impact_reaction_reverse_speed");
    r.impactReactionSpeedRamp = GetFloat(impact, "Impact_reaction_speed_ramp");
    r.impactReactionBlendOutTime = GetFloat(impact, "Impact_reaction_blend_out_time");
    const Node* combo = FindChild(row, "Combo");
    r.hasCombo = (combo != nullptr);
    if (combo) {
        r.comboEndPose = OptText(combo, "End_Pose");
        r.comboStartPose = OptText(combo, "Start_Pose");
        const Node* grid = FindChild(combo, "Anim_group_grid");
        for (const Node* ref = FindChild(grid, "Anim_group_ref"); ref; ref = NextSibling(grid, ref, "Anim_group_ref"))
            if (ref->text()) r.comboAnimGroupRefs.push_back(*ref->text());
    }
    const Node* be = FindChild(row, "Blood_Effect");
    r.bloodEffect = OptText(be, "Effect");
    r.bloodEffectRepeatCooldown = GetFloat(be, "Repeat_Cooldown");
    return r;
}
std::vector<MeleeMove> ParseAllMeleeMoves(const Document& doc) {
    std::vector<MeleeMove> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "MeleeMove"); row; row = NextSibling(table, row, "MeleeMove")) out.push_back(ParseMeleeMove(row));
    return out;
}

// ===========================================================================
// 7.2 melee_transition_states.xtbl
// ===========================================================================
MeleeTransitionState ParseMeleeTransitionState(const Node* row) {
    MeleeTransitionState r;
    r.name = OptText(row, "Name").value_or(std::string());
    r.animationState = OptText(row, "Animation_State");
    r.returnAction = OptText(row, "Return_Action");
    r.playerComboTimeMs = ReadInt32Always(row, "Player_Combo_Time_ms");
    r.npcComboMinTimeMs = ReadInt32Always(row, "NPC_Combo_Min_Time_ms");
    r.npcComboMaxTimeMs = ReadInt32Always(row, "NPC_Combo_Max_Time_ms");
    return r;
}
std::vector<MeleeTransitionState> ParseAllMeleeTransitionStates(const Document& doc) {
    std::vector<MeleeTransitionState> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Melee_Transition_State"); row; row = NextSibling(table, row, "Melee_Transition_State"))
        out.push_back(ParseMeleeTransitionState(row));
    return out;
}

// ===========================================================================
// 8. aim_drift.xtbl
// ===========================================================================
AimDriftProfile ParseAimDriftProfile(const Node* row) {
    AimDriftProfile r;
    r.name = OptText(row, "Name").value_or(std::string());
    r.turnSpeed = ReadFloatAlways(row, "turn_speed");
    const Node* bm = FindChild(row, "Bullet_miss");
    const Node* aiming = FindChild(bm, "Aiming");
    r.bulletMissAimingCloseRange = ReadFloatAlways(aiming, "Close_range");
    r.bulletMissAimingCloseAccuracy = ReadFloatAlways(aiming, "Close_accuracy");
    r.bulletMissAimingFarRange = ReadFloatAlways(aiming, "Far_range");
    r.bulletMissAimingFarAccuracy = ReadFloatAlways(aiming, "Far_accuracy");
    r.bulletMissAimingBouncesPerSec = ReadFloatAlways(aiming, "Bounces_per_sec");
    r.bulletMissAimingSpeedMultipler = ReadFloatAlways(aiming, "Speed_multipler");
    // [OPEN / NEEDS-EXE - spec-tables-weapons-combat.md section 8: "Recovery in
    // 0/20 real rows (section 18.6) and five undocumented elements in 20/20"].
    // Spec path kept unchanged; the empty case is reported, not hidden.
    const Node* recovery = FindChild(bm, "Recovery");
    if (!recovery) r.diagnostics.push_back(kAimDriftRecoveryEmptyDiagnostic);
    r.recoveryPenalty = ReadFloatAlways(recovery, "recover_penalty");
    r.recoveryTime = ReadFloatAlways(recovery, "recover_time");
    r.recoveryBulletsToUnsteady = ReadFloatAlways(recovery, "bullets_to_unsteady");
    const Node* firing = FindChild(bm, "Firing");
    r.firingStartBurstBoxPct = ReadFloatAlways(firing, "start_burst_box_pct");
    const Node* em = FindChild(row, "Explosive_Miss");
    r.explosiveMissZOffset = ReadFloatAlways(em, "explosive_z_offset");
    r.explosiveMissYOffset = ReadFloatAlways(em, "explosive_y_offset");
    {
        Always<float> lead = ReadFloatAlways(em, "lead_pct");
        if (lead.present) lead.value /= 100.0f;
        r.explosiveMissLeadPct = lead;
    }
    r.explosiveMissErrorRadiusMin = ReadFloatAlways(em, "explosive_error_radius_min");
    r.explosiveMissErrorRadiusMax = ReadFloatAlways(em, "explosive_error_radius_max");
    r.explosiveMissErrorFlat = ReadBoolAlways(em, "explosive_error_flat");
    return r;
}
std::vector<AimDriftProfile> ParseAllAimDriftProfiles(const Document& doc) {
    std::vector<AimDriftProfile> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Profile"); row; row = NextSibling(table, row, "Profile")) out.push_back(ParseAimDriftProfile(row));
    return out;
}

// ===========================================================================
// 9. aim_assist.xtbl
// ===========================================================================
namespace {
AimAssistAxisModel ParseAimAssistAxis(const Node* n) {
    AimAssistAxisModel r;
    r.distNearDist = ReadFloatAlways(FindChild(n, "dist"), "near_dist");
    r.fadeInRate = ReadFloatAlways(n, "fade_in_rate");
    r.fadeOutRate = ReadFloatAlways(n, "fade_out_rate");
    r.capsuleInnerMultiplier = ReadFloatAlways(n, "Capsule_inner_multiplier");
    r.capsuleOuterMultiplier = ReadFloatAlways(n, "Capsule_outer_multiplier");
    r.useCylinder = ReadBoolAlways(n, "Use_Cylinder");
    return r;
}
}  // namespace

AimAssistBlock ParseAimAssistBlock(const Node* row) {
    AimAssistBlock r;
    r.name = OptText(row, "Name").value_or(std::string());
    const Node* steering = FindChild(row, "steering");
    r.steerAmountX = ReadFloatAlways(steering, "steer_amount_x");
    r.steerAmountY = ReadFloatAlways(steering, "steer_amount_y");
    r.maxSteerAmountX = ReadFloatAlways(steering, "max_steer_amount_x");
    r.maxSteerAmountY = ReadFloatAlways(steering, "max_steer_amount_y");
    r.steering = ParseAimAssistAxis(steering);
    const Node* slowing = FindChild(row, "slowing");
    r.slowMultX = ReadFloatAlways(slowing, "slow_mult_x");
    r.slowMultY = ReadFloatAlways(slowing, "slow_mult_y");
    r.slowing = ParseAimAssistAxis(slowing);
    return r;
}
std::vector<AimAssistBlock> ParseAllAimAssistBlocks(const Document& doc) {
    std::vector<AimAssistBlock> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Aiming"); row; row = NextSibling(table, row, "Aiming")) out.push_back(ParseAimAssistBlock(row));
    return out;
}

// ===========================================================================
// 10.1 explosions.xtbl
// ===========================================================================
ExplosionRecord ParseExplosion(const Node* row) {
    ExplosionRecord r;
    r.name = OptText(row, "Name").value_or(std::string());
    r.panicReaction = OptText(row, "Panic_Reaction");
    r.radius = ReadFloatAlways(row, "Radius");
    r.decalRadiusOverride = GetFloat(row, "Decal_Radius_Override");
    r.coneAngleRadians = ReadDegAsRadIfPresent(row, "Cone_Angle");
    r.fireRadius = GetFloat(row, "FireRadius");
    r.aiSoundRadius = ReadFloatAlways(row, "AI_Sound_Radius");
    r.damageMin = ReadUInt32Always(row, "Damage_Min");
    r.damageMax = ReadUInt32Always(row, "Damage_Max");
    r.damageMinPlayer = ReadUInt32Always(row, "Damage_Min_Player");
    r.damageMaxPlayer = ReadUInt32Always(row, "Damage_Max_Player");
    r.playerVehicleDamageScalar = ReadFloatAlways(row, "Player_Vehicle_Damage_Scalar");
    r.impulse = ReadFloatAlways(row, "Impulse");
    if (auto d = GetFloat(row, "Redundant_Effect_Distance")) r.redundantEffectDistanceSquared = (*d) * (*d);
    r.effect = OptText(row, "Effect");
    const Node* sf = FindChild(row, "Sticky_Fire");
    r.stickyFireConeSpreadEffect = OptText(sf, "Cone_Spread_Effect");
    r.stickyFireCircularSpreadEffect = OptText(sf, "Circular_Spread_Effect");
    r.stickyFireLargeStickyEffect = OptText(sf, "Large_Sticky_Effect");
    {
        const Node* small = FindChild(sf, "Small_Sticky_Effects");
        for (const Node* e = FindChild(small, "Effect"); e; e = NextSibling(small, e, "Effect")) {
            StickySmallEffect se;
            se.effect = OptText(e, "Sticky_Effect");
            se.min = ReadUInt32Always(e, "Min");
            se.max = ReadUInt32Always(e, "Max");
            r.stickyFireSmallEffects.push_back(std::move(se));
        }
    }
    {
        const std::string* ap = ChildText(sf, "Always_Produce");
        r.stickyFireAlwaysProduce = (ap != nullptr) && NameEquals(*ap, "yes");
    }
    r.groundfire = OptText(row, "Groundfire");
    const Node* se = FindChild(row, "Screen_Effects");
    r.screenEffectsPresent = (se != nullptr);
    r.screenEffectsDelayTime = ReadFloatAlways(se, "Delay_Time");
    r.screenEffectsRampUpTime = ReadFloatAlways(se, "Ramp_Up_Time");
    r.screenEffectsFullStrengthDurationTime = ReadFloatAlways(se, "Full_Strength_Duration_Time");
    r.screenEffectsDecayTime = ReadFloatAlways(se, "Decay_Time");
    r.screenEffectsAttenStart = ReadFloatAlways(se, "Atten_Start");
    r.screenEffectsAttenEnd = ReadFloatAlways(se, "Atten_End");
    const Node* tone = FindChild(se, "Tone");
    const Node* tint = FindChild(tone, "Tint");
    r.screenEffectsTintR = GetInt32(tint, "R");
    r.screenEffectsTintG = GetInt32(tint, "G");
    r.screenEffectsTintB = GetInt32(tint, "B");
    r.screenEffectsTintScale = GetFloat(tone, "Tint_scale");
    r.screenEffectsSaturation = GetFloat(tone, "Saturation");
    r.screenEffectsLockedAttenuation = GetBool(se, "Locked_Attenuation").value_or(false);
    r.refractionScreenEffectHash = NameHashOrZero(ChildText(row, "Refraction_Screen_Effect"));
    const Node* flagsNode = FindChild(row, "Flags");
    r.causesElectricRagdoll = HasFlag(flagsNode, "Causes Electric Ragdoll");
    r.causesPlayerTinnitus = HasFlag(flagsNode, "Causes Player Tinnitus");
    r.causesVomit = HasFlag(flagsNode, "Causes Vomit");
    r.penetratesWorld = HasFlag(flagsNode, "Penetrates World");
    r.noRagdoll = HasFlag(flagsNode, "No Ragdoll");
    const Node* csi = FindChild(row, "Camera_Shake_Info");
    r.cameraShake = OptText(csi, "Camera_Shake");
    r.cameraShakeMaximumIntensity = ReadFloatAlways(csi, "Camera_Shake_Maximum_Intensity");
    r.cameraShakeRadius = ReadFloatAlways(csi, "Camera_Shake_Radius");
    return r;
}
std::vector<ExplosionRecord> ParseAllExplosions(const Document& doc) {
    std::vector<ExplosionRecord> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Explosion"); row; row = NextSibling(table, row, "Explosion")) out.push_back(ParseExplosion(row));
    return out;
}

// ===========================================================================
// 10.2 continuous_explosions.xtbl
// ===========================================================================
namespace {
constexpr std::string_view kContinuousExplosionTargetTypeNames[3] = {"default", "in_car", "in_aircraft"};
}  // namespace

ContinuousExplosion ParseContinuousExplosion(const Node* row) {
    ContinuousExplosion r;
    r.name = OptText(row, "Name").value_or(std::string());
    const Node* ed = FindChild(row, "Explosion_Data");
    r.explosion = OptText(ed, "Explosion");
    r.explosionNum = GetInt32(ed, "Explosion_Num");
    r.spreadMinRadians = ReadDegAsRadIfPresent(ed, "Spread_Min");
    r.spreadMaxRadians = ReadDegAsRadIfPresent(ed, "Spread_Max");
    if (auto v = GetFloat(ed, "Cooldown_min")) r.cooldownMinMs = static_cast<int32_t>(*v * 1000.0f);
    if (auto v = GetFloat(ed, "Cooldown_max")) r.cooldownMaxMs = static_cast<int32_t>(*v * 1000.0f);
    const Node* ai = FindChild(row, "Approach_Info");
    r.approachAngleMinRadians = ReadDegAsRadIfPresent(ai, "Approach_Angle_Min");
    r.approachAngleMaxRadians = ReadDegAsRadIfPresent(ai, "Approach_Angle_Max");
    r.approachDist = GetFloat(ai, "Approach_Dist");
    if (auto v = GetFloat(ai, "Launch_Dist_Min")) r.launchDistMinSquared = (*v) * (*v);
    const Node* audioInfo = FindChild(row, "Audio_Info");
    r.whizSound = OptText(audioInfo, "Whiz_Sound");
    r.avgLength = GetFloat(audioInfo, "Avg_Length");

    const Node* ti = FindChild(row, "Target_Info");
    if (ti) {
        const auto& kids = ti->children();
        for (size_t i = 0; i + 3 < kids.size(); i += 4) {
            int idx = EnumIndex(kids[i], kContinuousExplosionTargetTypeNames, 3);
            ContinuousExplosionTargetInfo info;
            info.present = true;
            info.radiusMin = ReadFloatAlways(kids[i + 1]);
            info.radiusMax = ReadFloatAlways(kids[i + 2]);
            Always<float> fov = ReadFloatAlways(kids[i + 3]);
            if (fov.present) fov.value = (fov.value * kDegToRad) * 0.5f;
            info.targetFovHalfAngleRadians = fov;
            if (idx == 0) r.targetDefault = info;
            else if (idx == 1) r.targetInCar = info;
            else if (idx == 2) r.targetInAircraft = info;
        }
    }
    return r;
}
std::vector<ContinuousExplosion> ParseAllContinuousExplosions(const Document& doc) {
    std::vector<ContinuousExplosion> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Continuous_explosion"); row; row = NextSibling(table, row, "Continuous_explosion"))
        out.push_back(ParseContinuousExplosion(row));
    return out;
}

// ===========================================================================
// 11.1 combat_actions.xtbl
// ===========================================================================
CombatActionOverrideRow ParseCombatActionOverride(const Node* row) {
    CombatActionOverrideRow r;
    r.name = OptText(row, "Name").value_or(std::string());
    r.minRepeatTimeMs = ReadUInt16Always(row, "Min_repeat_time");
    r.autoAbortMs = ReadUInt16Always(row, "Auto_abort_ms");
    r.noInterruptMs = ReadUInt16Always(row, "No_interrupt_ms");
    r.refreshDesireIntervalMs = ReadUInt16Always(row, "refresh_desire_interval");
    const Node* precondition = FindChild(row, "precondition");
    r.preconditionFlags = static_cast<uint8_t>(FlagBits(FindChild(precondition, "pre_flags"), kCombatActionPreconditionFlagNames));
    return r;
}
std::vector<CombatActionOverrideRow> ParseAllCombatActionOverrides(const Document& doc) {
    std::vector<CombatActionOverrideRow> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "actions"); row; row = NextSibling(table, row, "actions"))
        out.push_back(ParseCombatActionOverride(row));
    return out;
}

// ===========================================================================
// 11.2 combat_tricks.xtbl
// ===========================================================================
CombatTricksRow ParseCombatTricks(const Document& doc) {
    CombatTricksRow r;
    const Node* row = FindChild(doc.table(), "Combat_Tricks");
    r.defaultDurationMs = ReadFloatAlways(row, "Default_Duration");
    r.recordDisplayTimeMs = GetFloat(row, "Record_Display_Time");
    r.recordQueueTimeMs = GetFloat(row, "Record_Queue_Time");
    r.recordThreshold = ReadFloatAlways(row, "Record_Threshold");
    for (size_t i = 0; i < 18; ++i) {
        const Node* trick = FindChild(row, kCombatTrickNames[i]);
        CombatTrickDescriptor& d = r.tricks[i];
        d.present = (trick != nullptr);
        if (auto v = GetFloat(trick, "Duration")) d.durationMs = static_cast<int32_t>(*v * 1000.0f);
        d.maxRespect = ReadInt32Always(trick, "Max_Respect");
        d.maxLifetimeRespect = ReadInt32Always(trick, "Max_Lifetime_Respect");
        d.maxCash = ReadFloatAlways(trick, "Max_Cash");
        d.minValue = ReadFloatAlways(trick, "Min_Value");
        d.maxValue = ReadFloatAlways(trick, "Max_Value");
        if (i == 7) {  // Multi_Kill
            if (auto v = GetFloat(trick, "Kill_Duration")) d.killDurationMs = static_cast<int32_t>(*v * 1000.0f);
        }
    }
    return r;
}

// ===========================================================================
// 12.1 weapon_tracers.xtbl
// ===========================================================================
WeaponTracerRecord ParseWeaponTracer(const Node* row) {
    WeaponTracerRecord r;
    r.name = OptText(row, "Name").value_or(std::string());
    r.effect = OptText(row, "Effect");
    r.emitterEffect = OptText(row, "Emitter_Effect");
    r.maxParticles = ReadInt32Always(row, "Max_Particles");
    r.lifetime = ReadFloatAlways(row, "Lifetime");
    const Node* ric = FindChild(row, "Ricochet");
    r.ricochetChance = GetFloat(ric, "Chance");
    r.ricochetDistance = GetFloat(ric, "Distance");
    r.velocityScale = ReadFloatAlways(row, "Velocity_Scale");
    r.ricochetVelocityScale = ReadFloatAlways(ric, "Ricochet_velocity_scale");
    r.hotLengthSize = GetFloat(row, "Hot_Length_Size");
    return r;
}
std::vector<WeaponTracerRecord> ParseAllWeaponTracers(const Document& doc) {
    std::vector<WeaponTracerRecord> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Weapon_Tracers"); row; row = NextSibling(table, row, "Weapon_Tracers"))
        out.push_back(ParseWeaponTracer(row));
    return out;
}

// ===========================================================================
// 12.2 weapon_tracer_materials.xtbl
// ===========================================================================
TracerMaterialRow ParseTracerMaterial(const Node* row) {
    TracerMaterialRow r;
    r.materialName = OptText(row, "Name").value_or(std::string());
    r.tracerDampener = ReadFloatAlways(row, "Tracer_Dampener");
    return r;
}
std::vector<TracerMaterialRow> ParseAllTracerMaterials(const Document& doc) {
    std::vector<TracerMaterialRow> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Tracer_Material"); row; row = NextSibling(table, row, "Tracer_Material"))
        out.push_back(ParseTracerMaterial(row));
    return out;
}

// ===========================================================================
// 13.1 crib_weapons.xtbl
// ===========================================================================
std::vector<CribWeaponEntry> ParseCribWeapons(const Document& doc) {
    std::vector<CribWeaponEntry> out;
    const Node* table = doc.table();
    const Node* crib = FindChild(table, "Crib_Weapons");
    const Node* list = FindChild(crib, "Weapons_List");
    for (const Node* e = FindChild(list, "Entry"); e; e = NextSibling(list, e, "Entry")) {
        CribWeaponEntry ce;
        ce.weapon = OptText(e, "Weapon");
        ce.unlocked = ReadBoolAlways(e, "Unlocked");
        out.push_back(std::move(ce));
    }
    return out;
}

// ===========================================================================
// 13.2 store_weapons.xtbl
// ===========================================================================
std::vector<StoreWeaponEntry> ParseStoreWeapons(const Document& doc) {
    std::vector<StoreWeaponEntry> out;
    const Node* table = doc.table();
    const Node* store = FindChild(table, "Store_Weapons");
    const Node* list = FindChild(store, "Weapons_List");
    for (const Node* e = FindChild(list, "Entry"); e; e = NextSibling(list, e, "Entry")) {
        StoreWeaponEntry se;
        se.mission = OptText(e, "Mission");
        se.weapon = OptText(e, "Weapon");
        se.unlocked = ReadBoolAlways(e, "Unlocked");
        se.numHoods = GetInt32(e, "Num_Hoods");
        se.angle = GetFloat(e, "Angle");
        se.distance = GetFloat(e, "Distance");
        se.offset = GetFloat(e, "Offset");
        out.push_back(std::move(se));
    }
    return out;
}

// ===========================================================================
// 14.1 strafe_angles.xtbl
// ===========================================================================
StrafeAnglesRow ParseStrafeAngles(const Node* row) {
    StrafeAnglesRow r;
    r.name = OptText(row, "Name").value_or(std::string());
    r.forwardRadians = ReadDegAsRadAlways(row, "Forward");
    r.rightRadians = ReadDegAsRadAlways(row, "Right");
    r.backwardRightRadians = ReadDegAsRadAlways(row, "Backward_Right");
    r.backwardRadians = ReadDegAsRadAlways(row, "Backward");
    r.backwardLeftRadians = ReadDegAsRadAlways(row, "Backward_Left");
    r.leftRadians = ReadDegAsRadAlways(row, "Left");
    const Node* utl = FindChild(row, "Use_Turn_Limits");
    r.useTurnLimits = (utl != nullptr);
    {
        Always<float> left = ReadFloatAlways(utl, "Left_Limit");
        if (left.present) left.value = -(360.0f - left.value) * kDegToRad;
        r.turnLimitLeftRadians = left;
    }
    r.turnLimitRightRadians = ReadDegAsRadAlways(utl, "Right_Limit");
    return r;
}
std::vector<StrafeAnglesRow> ParseAllStrafeAngles(const Document& doc) {
    std::vector<StrafeAnglesRow> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, "Strafe_Angles"); row; row = NextSibling(table, row, "Strafe_Angles"))
        out.push_back(ParseStrafeAngles(row));
    return out;
}

// ===========================================================================
// 14.2 taunting.xtbl
// ===========================================================================
TauntingRow ParseTaunting(const Document& doc) {
    TauntingRow r;
    const Node* row = FindChild(doc.table(), "Taunting");
    r.maxTaunts = ReadInt32Always(row, "Max_Taunts");
    r.minTaunts = ReadInt32Always(row, "Min_Taunts");
    r.aggroTauntValue = ReadInt32Always(row, "Aggro_Taunt_Value");
    r.deathTauntValue = ReadInt32Always(row, "Death_Taunt_Value");
    r.deathTauntDelayMs = ReadSecAsMsAlways(row, "Death_Taunt_Delay");
    r.maxTimeBetweenTauntsMs = ReadSecAsMsAlways(row, "Max_Time_Between_Taunts");
    {
        Always<float> dist = ReadFloatAlways(row, "Death_Taunt_Distance");
        if (dist.present) dist.value *= dist.value;
        r.deathTauntDistanceSquared = dist;
    }
    {
        Always<float> pct = ReadFloatAlways(row, "Taunt_Complet_Percent");
        if (pct.present) pct.value /= 100.0f;
        r.tauntCompletPercent = pct;
    }
    r.awardMultiplier = ReadFloatAlways(row, "Award_Multiplier");
    r.firstTauntCash = ReadFloatAlways(row, "First_Taunt_Cash");
    r.firstTauntRespect = ReadInt32Always(row, "First_Taunt_Respect");
    r.maxLifetimeRespect = ReadInt32Always(row, "Max_Lifetime_Respect");
    return r;
}

// ===========================================================================
// 14.3 hostage.xtbl
// ===========================================================================
HostageRow ParseHostage(const Document& doc) {
    HostageRow r;
    const Node* row = FindChild(doc.table(), "Hostage");
    r.minNotoriety = GetInt32(row, "Min_Notoriety");
    r.maxLifetimeRespect = GetInt32(row, "Max_Lifetime_Respect");
    const Node* vcs = FindChild(row, "Vehicle_Classes");
    for (const Node* vc = FindChild(vcs, "Vehicle_Class"); vc; vc = NextSibling(vcs, vc, "Vehicle_Class")) {
        HostageVehicleClassRow cr;
        cr.classIndex = EnumIdx(vc, "Class_Name", kHostageVehicleClassNames);
        const Node* dls = FindChild(vc, "Difficulty_Levels");
        for (const Node* dl = FindChild(dls, "Difficulty_Level"); dl; dl = NextSibling(dls, dl, "Difficulty_Level")) {
            HostageDifficultyLevel lvl;
            lvl.numHostages = GetInt32(dl, "Num_Hostages");
            lvl.minEvasionTimeMs = ReadSecAsMsIfPresent(dl, "Min_Evasion_Time");
            lvl.maxEvasionTimeMs = ReadSecAsMsIfPresent(dl, "Max_Evasion_Time");
            lvl.notorietyPerSec = GetInt32(dl, "Notoriety_Per_Sec");
            lvl.respect = GetInt32(dl, "Respect");
            lvl.cash = GetFloat(dl, "Cash");
            cr.difficultyLevels.push_back(std::move(lvl));
        }
        r.vehicleClasses.push_back(std::move(cr));
    }
    return r;
}

// ===========================================================================
// 14.4 windshield_cannon.xtbl
// ===========================================================================
WindshieldCannonRow ParseWindshieldCannon(const Document& doc) {
    WindshieldCannonRow r;
    const Node* row = FindChild(doc.table(), "Windshield_Cannon");
    r.maxDistance = ReadFloatAlways(row, "Max_Distance");
    r.minDistance = ReadFloatAlways(row, "Min_Distance");
    {
        Always<float> rt = ReadFloatAlways(row, "Record_Threshold");
        if (rt.present) rt.value /= 100.0f;
        r.recordThreshold = rt;
    }
    r.recordDisplayTimeMs = ReadSecAsMsIfPresent(row, "Record_Display_Time");
    r.recordQueueTimeMs = ReadSecAsMsIfPresent(row, "Record_Queue_Time");
    r.maxRespect = ReadInt32Always(row, "Max_Respect");
    r.maxLifetimeRespect = ReadInt32Always(row, "Max_Lifetime_Respect");
    r.maxCash = ReadFloatAlways(row, "Max_Cash");
    return r;
}

}  // namespace sr3tables_weapons
