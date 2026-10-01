#pragma once

// sr3tables_weapons - typed readers for the remaining 20 tables of the
// weapons/combat group (spec-tables-weapons-combat.md sections 4-14). See
// weapons.h for weapons.xtbl / weapon_categories.xtbl and for the shared
// conventions (Always<T> vs optional<T>, cross-table names kept as text,
// the one degrees->radians constant, what is deliberately NOT modelled).
//
// `store_weapon_lightset.xtbl` (spec section 13.3) is NOT implemented here:
// the spec explicitly states its document schema is the shared lightset
// format, which belongs to a different group and is OPEN in this document
// ("The document schema itself: OPEN here - belongs to another group").
// That leaves 20 tables in this file (21 with weapons.h's 2 = the 22 minus
// the one skipped).

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sr3tables_weapons/weapons.h"
#include "sr3xtbl/xtbl.h"

namespace sr3tables_weapons {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;

// ===========================================================================
// 4. weapon_upgrades.xtbl - per-weapon upgrade patches (spec section 4)
// ===========================================================================

// Table at spec section 4.2, `0x011888BC`: absent/unmatched `<name>_OM` => Replace.
enum class OperationMode : int32_t { Replace = -1, Additive = 0, Multiplicative = 1, Removal = 2 };

// One override element: its parsed value (read with the exact same readers
// weapons.xtbl §2 uses, so units/enums/sub-blocks match 1:1) plus its
// sibling `_OM` operation mode. Only constructed when the override element
// is itself present (spec: "only when the element is present").
template <class T>
struct WeaponOverride {
    T value{};
    OperationMode mode = OperationMode::Replace;
};

// Damage_Max / Damage_Min, as an override (weapons.xtbl models these as two
// flat fields on Weapon; the upgrade reader needs a self-contained pair).
struct DamageMaxMinOverride {
    Always<float> npcDamage;
    Always<float> playerDamage;
};

// Flags, as an override: the engine compares the parsed three words against
// the base weapon's and patches all three together only if any differs
// (spec 4.2); a single-row parse cannot know the base, so this simply
// exposes the row's own Flags block (same vocabulary as weapons.xtbl §2.4).
struct FlagsOverride {
    uint32_t flagsA = 0, flagsB = 0, flagsC = 0;
};

struct WeaponUpgrade {
    std::string name;                          // Name
    std::optional<std::string> editorCategory;   // _Editor/Category, raw text; REQUIRED by the real loader (no fallback)
    std::optional<std::string> targetWeaponName;  // the text after the LAST ':' of editorCategory - the weapon this upgrade patches
    std::optional<std::string> upgradeDescription;  // upgrade_description (text; the engine's handle computation is out of scope)
    std::optional<uint32_t> upgradePrice;             // upgrade_price (i)

    // --- override vocabulary (spec section 4.3), each optional + its OM ---
    std::optional<WeaponOverride<std::string>> displayNameOverride;
    std::optional<WeaponOverride<std::string>> descriptionOverride;
    std::optional<WeaponOverride<std::string>> bitmapOverride;
    std::optional<WeaponOverride<std::string>> objectItemOverride;

    std::optional<WeaponOverride<std::string>> animationGroup;
    std::optional<WeaponOverride<std::string>> fineAimAnimationGroup;
    std::optional<WeaponOverride<std::string>> reloadAnimationGroup;
    std::optional<WeaponOverride<std::string>> strafeAngles;
    std::optional<WeaponOverride<std::string>> muzzleEffect;
    std::optional<WeaponOverride<std::string>> altMuzzleEffect;
    std::optional<WeaponOverride<std::string>> meleeEffect;
    std::optional<WeaponOverride<std::string>> fireConeImpactEffect;

    std::optional<WeaponOverride<TracerInfo>> tracerInfo;
    std::optional<WeaponOverride<std::vector<ConstantEffect>>> constantEffects;
    std::optional<WeaponOverride<std::string>> brass;
    std::optional<WeaponOverride<float>> soundRadius;
    std::optional<WeaponOverride<TargetLockon>> targetLockon;

    std::optional<WeaponOverride<int32_t>> triggerType;      // enum index, -1 if unmatched
    std::optional<WeaponOverride<int32_t>> altTriggerType;
    std::optional<WeaponOverride<std::string>> ammo;
    std::optional<WeaponOverride<std::string>> altAmmo;
    std::optional<WeaponOverride<uint16_t>> magazineSize;
    std::optional<WeaponOverride<float>> rangeMax;
    std::optional<WeaponOverride<std::string>> npcAimDrift;

    std::optional<WeaponOverride<TimeManagementBlock>> timeManagement;
    std::optional<WeaponOverride<TimeManagementBlock>> altTimeManagement;
    std::optional<WeaponOverride<DamageMaxMinOverride>> damageMax;
    std::optional<WeaponOverride<DamageMaxMinOverride>> damageMin;
    std::optional<WeaponOverride<std::string>> explosion;
    std::optional<WeaponOverride<std::string>> underwaterExplosion;
    std::optional<WeaponOverride<float>> damageMaxDist;
    std::optional<WeaponOverride<float>> damageMinDist;

    std::optional<WeaponOverride<FlatSpreadMetrics>> flatSpreadMetrics;
    std::optional<WeaponOverride<float>> fireConeAngleDegrees;  // raw authored degrees; the patch applies §2's cos(half-angle) transform
    std::optional<WeaponOverride<float>> fireConeLength;
    std::optional<WeaponOverride<FireConeMetrics>> fireConeMetrics;
    std::optional<WeaponOverride<uint8_t>> shotsPerRound;

    std::optional<WeaponOverride<PlayerSpreadBlock>> playerWeaponSpread;
    std::optional<WeaponOverride<PlayerSpreadBlock>> playerAltWeaponSpread;
    std::optional<WeaponOverride<NpcSpreadBlock>> npcWeaponSpread;
    std::optional<WeaponOverride<NpcSpreadBlock>> npcAltWeaponSpread;

    std::optional<WeaponOverride<float>> ragdollForceShoot;
    std::optional<WeaponOverride<float>> objectBulletHitImpulseMagnitude;
    std::optional<WeaponOverride<RagdollInfo>> ragdollInfo;
    std::optional<WeaponOverride<ProjectileInfo>> projectileInfo;
    std::optional<WeaponOverride<std::vector<std::pair<std::string, std::optional<std::string>>>>> meleeMaterialEffects;

    std::optional<WeaponOverride<float>> meleeDamageToAnchoredScaler;
    std::optional<WeaponOverride<float>> vehicleDamageScale;
    std::optional<WeaponOverride<float>> playerVehicleDamageScale;
    std::optional<WeaponOverride<std::string>> meleeAttackInfo;
    std::optional<WeaponOverride<ChargeReleaseInfo>> chargeReleaseInfo;

    std::optional<WeaponOverride<CameraInfo>> cameraInfo;
    std::optional<WeaponOverride<BurstFireInfo>> burstFireInfo;
    std::optional<WeaponOverride<std::string>> overrideBulletImpactEffect;
    std::optional<WeaponOverride<std::string>> altOverrideBulletImpactEffect;
    std::optional<WeaponOverride<std::string>> penetratingEndPointExplosion;
    std::optional<WeaponOverride<std::string>> altPenetratingEndPointExplosion;
    std::optional<WeaponOverride<OverheatInfo>> overheatInfo;
    std::optional<WeaponOverride<std::vector<EffectSituationEntry>>> effectSituations;

    std::optional<WeaponOverride<MeleeDamageOverrides>> meleeDamageOverrides;
    std::optional<WeaponOverride<FlagsOverride>> flags;
};

WeaponUpgrade ParseWeaponUpgrade(const Node* row);
std::vector<WeaponUpgrade> ParseAllWeaponUpgrades(const Document& doc);

// ===========================================================================
// 5. ammo.xtbl (spec section 5)
// ===========================================================================
inline constexpr std::string_view kAmmoFlagNames[12] = {
    "drops with weapon", "thrown", "lethal", "bullet", "projectile", "fire",
    "water", "sewage", "incendiary", "penetrating", "laser", "armor piercing",
};

struct AmmoRecord {
    std::string name;
    uint32_t flags = 0;                                // Flags/Flag bitmask (12 literals, contiguous)
    Always<uint32_t> maxInReserve;                        // Max_in_Reserve (a)
    uint8_t invSlot = 0xFF;                                 // Inv_Slot, same 11 names as weapons.Inv_Slot; 0xFF if absent/unmatched
    std::optional<std::string> upgradableAmmoWeapon;          // Upgradable_Ammo/Weapon (name text; CRC'd by the engine)
    Always<uint32_t> upgradableAmmoLevel2;                       // Upgradable_Ammo/Level_2_Ammo (a); only meaningful if Upgradable_Ammo present
    Always<uint32_t> upgradableAmmoLevel3;                         // .../Level_3_Ammo (a)
    Always<uint32_t> upgradableAmmoLevel4;                           // .../Level_4_Ammo (a)
    Always<uint32_t> storeCost;                                        // store/cost (a); NOT written at all if <store> absent
    Always<uint32_t> storeClipSize;                                      // store/clip_size (a)
};

AmmoRecord ParseAmmo(const Node* row);
std::vector<AmmoRecord> ParseAllAmmo(const Document& doc);

// ===========================================================================
// 6. weapon_melee_attacks.xtbl (spec section 6)
// ===========================================================================
// Each slot names a melee.xtbl MeleeMove (resolved by CRC of the row's text
// in the real loader); stored here as raw name text. A caller holding both
// this table's rows and melee.xtbl's rows (section 7.1) can join them with
// sr3xtbl::NameHash, since melee.xtbl's key is CRC(Name) (section 7.1).
struct MeleeAttackSet {
    std::string name;
    std::optional<std::string> standingPrimary, standingSecondary;
    std::optional<std::string> movingPrimary, movingSecondary;
    std::optional<std::string> crouching, crouchMoving;
    std::optional<std::string> proneAttackPrimary, proneAttackSecondary;
    std::optional<std::string> hardPrimary, hardSecondary;
    std::optional<std::string> nonFancyPrimary, nonFancySecondary;
    std::optional<std::string> standingSynced, movingSynced;
    Always<float> targetSearchRange;  // Target_Search_Range (a)
};

MeleeAttackSet ParseMeleeAttackSet(const Node* row);
std::vector<MeleeAttackSet> ParseAllMeleeAttackSets(const Document& doc);

// ===========================================================================
// 7.1 melee.xtbl (spec section 7.1)
// ===========================================================================
inline constexpr std::string_view kAttackLimbNames[6] = {
    "Right hand", "Left hand", "Right foot", "Left foot", "Both hands", "Weapon",
};

struct MeleeActiveAttackInfo {  // one of up to 6 positional Active_Attack_Infos children
    std::optional<std::string> qteHudText;    // QTE_HUD (the engine hashes this text; stored as text)
    Always<float> successExtraDamage;           // Success_Extra_Damage (a)
    std::optional<std::string> successEffect;     // Success_Effect (name text); -1 if absent
    std::optional<std::string> failEffect;           // Fail_Effect (name text); -1 if absent
    std::optional<std::string> effectTag;              // Effect_Tag (interned string pool; stored as text)
    Always<bool> effectOnVictim;                          // Effect_on_Victim (a)
};

struct MeleeMove {
    std::string name;
    std::optional<std::string> attackAnim;       // AttackAnim (animation-state name text; index resolver out of scope); -1 if absent/unmatched
    std::optional<std::string> syncedMove;         // SyncedMove (anim_synced.xtbl name text; index resolver out of scope); 0xFFFF if absent/unmatched
    std::optional<std::string> impactFx;             // Impact/ImpactFX (name text); -1 if absent
    std::optional<std::string> impactHumanEffect;      // Impact/Impact_Human_Effect (name text); -1 if absent
    // +0x38 (the hard-coded "Melee_blood" effect lookup) is not element-driven - not modelled.
    sr3xtbl::Vec3Result impactDir;                        // Impact/ImpactDir/X,Y,Z; the engine NORMALISES this on load (not reproduced - convention 3)
    Always<float> impactForce;                              // Impact/ImpactForce (a)
    Always<float> defaultDamageNpc;                           // DefaultDamage/NPCDamage (a)
    Always<float> defaultDamagePlayer;                          // DefaultDamage/PlayerDamage (a)
    Always<float> defaultDamageOnline;                            // DefaultDamage/OnlineDamage (a)
    std::optional<int32_t> ragdollGetupTimeMs;                      // Ragdoll_Getup_Time_ms (i), default 0
    uint32_t explosionHash = 0;                                       // Explosion: CRC of the name, stored UNRESOLVED (unlike weapons.xtbl); 0 if absent
    std::vector<MeleeActiveAttackInfo> activeAttackInfos;               // Active_Attack_Infos, positional children, capacity 6
    int attackLimb = 0;                                                   // Impact/AttackLimb, 6-name enum; 0 if absent, -1 if present & unmatched
    int attackLimbNpc = 0;                                                  // Impact/AttackLimbNPC; NPC defaults to the player value when absent
    // Combined +0xF8 flag word: Processing_flags/Flag + Attack_is_for/Flag +
    // Attack_Results/Flag + Testicular_Assault (bit 0x800000) + a non-empty
    // Active_Attack_Infos (bit 0x2000000) - spec section 7.1.
    uint32_t flagsF8 = 0;
    bool testicularAssault = false;                                          // Testicular_Assault (row-level bool); also ORed into flagsF8 bit 0x800000
    std::optional<int32_t> impactReactionTimeMs;                               // Impact/Impact_reaction_time (i), else 0
    std::optional<float> impactReactionReverseSpeed;                            // Impact/Impact_reaction_reverse_speed (i), else 0
    std::optional<float> impactReactionSpeedRamp;                                // Impact/Impact_reaction_speed_ramp (i), else 0
    std::optional<float> impactReactionBlendOutTime;                              // Impact/Impact_reaction_blend_out_time (i), else 0
    bool hasCombo = false;                                                          // whether <Combo> existed
    std::optional<std::string> comboEndPose;                                         // Combo/End_Pose (transition-state name text); only assigned when Combo exists
    std::optional<std::string> comboStartPose;                                         // Combo/Start_Pose (name text)
    std::vector<std::string> comboAnimGroupRefs;                                         // Combo/Anim_group_grid/Anim_group_ref (name text list)
    std::optional<std::string> bloodEffect;                                                // Blood_Effect/Effect (name text); -1 if absent
    std::optional<float> bloodEffectRepeatCooldown;                                          // Blood_Effect/Repeat_Cooldown; default -1.0
};

MeleeMove ParseMeleeMove(const Node* row);
std::vector<MeleeMove> ParseAllMeleeMoves(const Document& doc);

// ===========================================================================
// 7.2 melee_transition_states.xtbl (spec section 7.2)
// ===========================================================================
struct MeleeTransitionState {
    std::string name;                          // bounded copy, 0x20 bytes in the engine; kept in full here
    std::optional<std::string> animationState;   // Animation_State (name text; index resolver out of scope); -1 if unmatched
    std::optional<std::string> returnAction;       // Return_Action (name text); -1 if absent
    Always<int32_t> playerComboTimeMs;                // Player_Combo_Time_ms (a)
    Always<int32_t> npcComboMinTimeMs;                  // NPC_Combo_Min_Time_ms (a)
    Always<int32_t> npcComboMaxTimeMs;                    // NPC_Combo_Max_Time_ms (a)
    // +0x34..+0x84 (entry-move back-references and their count) are filled
    // by melee.xtbl's OWN load pass, not by this row's reader - not modelled.
};

MeleeTransitionState ParseMeleeTransitionState(const Node* row);
std::vector<MeleeTransitionState> ParseAllMeleeTransitionStates(const Document& doc);

// ===========================================================================
// 8. aim_drift.xtbl (spec section 8)
// ===========================================================================
// "Every float is an 'always' read." The parents (Bullet_miss, Aiming,
// Firing, Explosive_Miss) are not null-checked by the real reader either, so
// a well-formed profile carries all four (spec section 8) - not enforced here.
//
// [OPEN / NEEDS-EXE - spec-tables-weapons-combat.md section 8, "Review status
// (2026-09-30): NEEDS-EXE: `Recovery` in 0/20 real rows (section 18.6) and five
// undocumented elements in 20/20 (Team B 9.83)"]. The `Bullet_miss -> Recovery`
// path below is the spec's reading, but real data carries no `Recovery` element
// in any of 20 rows while `Penalties`, `Bonuses`, `lag_amount`, `lag_time`,
// `vertical_offset` occur in 20/20; the spec says the path may have been
// mis-associated. The reader keeps the spec path (no change to the values) but
// REPORTS the condition in `diagnostics` instead of silently returning empty
// recovery fields.
inline constexpr const char* kAimDriftRecoveryEmptyDiagnostic =
    "aim_drift Recovery empty - spec path suspect (NEEDS-EXE)";

struct AimDriftProfile {
    // Holds kAimDriftRecoveryEmptyDiagnostic when `Bullet_miss` has no `Recovery`
    // child (the three recovery* fields are then not present).
    std::vector<std::string> diagnostics;
    std::string name;
    Always<float> turnSpeed;                       // turn_speed (row level)
    Always<float> bulletMissAimingCloseRange;         // Bullet_miss/Aiming/Close_range
    Always<float> bulletMissAimingCloseAccuracy;        // .../Close_accuracy
    Always<float> bulletMissAimingFarRange;               // .../Far_range
    Always<float> bulletMissAimingFarAccuracy;              // .../Far_accuracy
    Always<float> bulletMissAimingBouncesPerSec;              // .../Bounces_per_sec
    Always<float> bulletMissAimingSpeedMultipler;                // .../Speed_multipler (misspelled in the reader itself, per spec)
    // recovery*: OPEN / NEEDS-EXE (spec section 8 review status; see the note above this struct).
    Always<float> recoveryPenalty;                            // Bullet_miss/Recovery/recover_penalty; documented default 1.0
    Always<float> recoveryTime;                                       // .../recover_time; documented default 500.0
    Always<float> recoveryBulletsToUnsteady;                            // .../bullets_to_unsteady; documented default -1.0
    Always<float> firingStartBurstBoxPct;                                 // Bullet_miss/Firing/start_burst_box_pct
    Always<float> explosiveMissZOffset;                                     // Explosive_Miss/explosive_z_offset
    Always<float> explosiveMissYOffset;                                       // .../explosive_y_offset
    Always<float> explosiveMissLeadPct;                                         // .../lead_pct, stored /100 (percentage -> fraction)
    Always<float> explosiveMissErrorRadiusMin;                                    // .../explosive_error_radius_min
    Always<float> explosiveMissErrorRadiusMax;                                      // .../explosive_error_radius_max
    Always<bool> explosiveMissErrorFlat;                                              // .../explosive_error_flat (u8 bool, always)
};

AimDriftProfile ParseAimDriftProfile(const Node* row);
std::vector<AimDriftProfile> ParseAllAimDriftProfiles(const Document& doc);

// ===========================================================================
// 9. aim_assist.xtbl (spec section 9)
// ===========================================================================
inline constexpr std::string_view kAimAssistBlockNames[5] = {"Normal", "Combat Ready", "Fine Aim", "Tank Skydiving", "Zoomed"};

struct AimAssistAxisModel {  // the "steering" or "slowing" sub-block
    Always<float> distNearDist;              // dist/near_dist (one authored float, replicated into 4 engine words)
    Always<float> fadeInRate;                  // fade_in_rate
    Always<float> fadeOutRate;                   // fade_out_rate
    Always<float> capsuleInnerMultiplier;          // Capsule_inner_multiplier; documented default 1.0
    Always<float> capsuleOuterMultiplier;            // Capsule_outer_multiplier; documented default 1.0
    Always<bool> useCylinder;                          // Use_Cylinder; documented default true
};

struct AimAssistBlock {
    std::string name;   // matched case-insensitively against the 5 fixed block names; an unmatched row is skipped by the real loader
    Always<float> steerAmountX, steerAmountY;         // steering/steer_amount_x, steer_amount_y
    Always<float> maxSteerAmountX, maxSteerAmountY;     // steering/max_steer_amount_x, max_steer_amount_y
    AimAssistAxisModel steering;                          // steering/*
    Always<float> slowMultX, slowMultY;                     // slowing/slow_mult_x, slow_mult_y; documented default 1.0
    AimAssistAxisModel slowing;                                // slowing/*
};

AimAssistBlock ParseAimAssistBlock(const Node* row);
std::vector<AimAssistBlock> ParseAllAimAssistBlocks(const Document& doc);

// ===========================================================================
// 10.1 explosions.xtbl (spec section 10.1)
// ===========================================================================
struct StickySmallEffect {  // Sticky_Fire/Small_Sticky_Effects/Effect, capacity 4
    std::optional<std::string> effect;  // Sticky_Effect (name text); -1 if absent
    Always<uint32_t> min;                 // Min (a)
    Always<uint32_t> max;                   // Max (a)
};

struct ExplosionRecord {
    std::string name;                              // bounded copy, 0x20 bytes
    std::optional<std::string> panicReaction;         // Panic_Reaction (id lookup table not decoded here; stored as text)
    Always<float> radius;                                // Radius (a)
    std::optional<float> decalRadiusOverride;              // Decal_Radius_Override (i); default Radius
    std::optional<float> coneAngleRadians;                   // Cone_Angle (i), degrees -> radians; default -1.0 (no cone)
    std::optional<float> fireRadius;                           // FireRadius (i); default 0
    Always<float> aiSoundRadius;                                  // AI_Sound_Radius (a)
    Always<uint32_t> damageMin;                                     // Damage_Min (a)
    Always<uint32_t> damageMax;                                       // Damage_Max (a)
    Always<uint32_t> damageMinPlayer;                                   // Damage_Min_Player (a)
    Always<uint32_t> damageMaxPlayer;                                     // Damage_Max_Player (a)
    Always<float> playerVehicleDamageScalar;                                // Player_Vehicle_Damage_Scalar (a)
    Always<float> impulse;                                                    // Impulse (a)
    std::optional<float> redundantEffectDistanceSquared;                        // Redundant_Effect_Distance (i); STORED SQUARED; default 0
    std::optional<std::string> effect;                                            // Effect (name text); -1 if absent
    std::optional<std::string> stickyFireConeSpreadEffect;                          // Sticky_Fire/Cone_Spread_Effect
    std::optional<std::string> stickyFireCircularSpreadEffect;                        // Sticky_Fire/Circular_Spread_Effect
    std::optional<std::string> stickyFireLargeStickyEffect;                             // Sticky_Fire/Large_Sticky_Effect
    std::vector<StickySmallEffect> stickyFireSmallEffects;                                // Sticky_Fire/Small_Sticky_Effects/Effect, capacity 4
    bool stickyFireAlwaysProduce = false;                                                    // Sticky_Fire/Always_Produce: text == "yes" case-insensitively ONLY (NOT "true")
    std::optional<std::string> groundfire;                                                    // Groundfire (id lookup table not decoded here; stored as text); 0 if absent
    bool screenEffectsPresent = false;                                                           // whether <Screen_Effects> existed
    Always<float> screenEffectsDelayTime;                                                          // Screen_Effects/Delay_Time (a); only if Screen_Effects present
    Always<float> screenEffectsRampUpTime;                                                           // .../Ramp_Up_Time (a)
    Always<float> screenEffectsFullStrengthDurationTime;                                                // .../Full_Strength_Duration_Time (a)
    Always<float> screenEffectsDecayTime;                                                                // .../Decay_Time (a)
    Always<float> screenEffectsAttenStart;                                                                  // .../Atten_Start (a)
    Always<float> screenEffectsAttenEnd;                                                                       // .../Atten_End (a)
    std::optional<int32_t> screenEffectsTintR, screenEffectsTintG, screenEffectsTintB;                            // Tone/Tint (component semantics of FUN_00DAD160 OPEN; raw R/G/B ints 0-255 as shipped)
    std::optional<float> screenEffectsTintScale;                                                                    // Tone/Tint_scale (i)
    std::optional<float> screenEffectsSaturation;                                                                     // Tone/Saturation (i)
    bool screenEffectsLockedAttenuation = false;                                                                        // Screen_Effects/Locked_Attenuation (bool)
    uint32_t refractionScreenEffectHash = 0;                                                                              // Refraction_Screen_Effect: CRC of the name; 0 if absent
    bool causesElectricRagdoll = false;    // Flags/"Causes Electric Ragdoll" (its own byte, not a shared bitmask - spec section 10.1)
    bool causesPlayerTinnitus = false;       // Flags/"Causes Player Tinnitus"
    bool causesVomit = false;                  // Flags/"Causes Vomit"
    bool penetratesWorld = false;                // Flags/"Penetrates World"
    bool noRagdoll = false;                        // Flags/"No Ragdoll"
    std::optional<std::string> cameraShake;          // Camera_Shake_Info/Camera_Shake (name text); 0 if the block is absent
    Always<float> cameraShakeMaximumIntensity;         // Camera_Shake_Info/Camera_Shake_Maximum_Intensity (a); 0 if the block is absent
    Always<float> cameraShakeRadius;                     // Camera_Shake_Info/Camera_Shake_Radius (a); 0 if the block is absent
};

ExplosionRecord ParseExplosion(const Node* row);
std::vector<ExplosionRecord> ParseAllExplosions(const Document& doc);

// ===========================================================================
// 10.2 continuous_explosions.xtbl (spec section 10.2)
// ===========================================================================
// The real loader REJECTS a whole row (uncounted) on any failed range check
// below; this reader does not reproduce that rejection (it is runtime
// validation of already-parsed values, not schema) - it parses every row and
// leaves range compliance to a caller/validator. At most 3 rows are kept by
// the real loader; not enforced here either.
struct ContinuousExplosionTargetInfo {
    bool present = false;                    // whether this Target_Type slot was authored
    Always<float> radiusMin;                    // Radius_Min; spec range [0,500]
    Always<float> radiusMax;                      // Radius_Max; spec range [0,500], >= radiusMin
    Always<float> targetFovHalfAngleRadians;         // Target_FOV, integer degrees <=360 -> HALF-angle radians
};

struct ContinuousExplosion {
    // The engine keeps only CRC(Name), not the text; kept here anyway as a schema field.
    std::string name;
    std::optional<std::string> explosion;             // Explosion_Data/Explosion (name text)
    std::optional<int32_t> explosionNum;                 // Explosion_Data/Explosion_Num; spec range 1..3
    std::optional<float> spreadMinRadians;                 // Explosion_Data/Spread_Min, integer degrees [0,90] -> radians
    std::optional<float> spreadMaxRadians;                   // Explosion_Data/Spread_Max, integer degrees [0,90] -> radians, >= spreadMin
    std::optional<int32_t> cooldownMinMs;                      // Explosion_Data/Cooldown_min, seconds -> ms (integer)
    std::optional<int32_t> cooldownMaxMs;                         // Explosion_Data/Cooldown_max, seconds -> ms, >= cooldownMin
    std::optional<float> approachAngleMinRadians;                    // Approach_Info/Approach_Angle_Min, integer degrees [0,90] -> radians
    std::optional<float> approachAngleMaxRadians;                       // Approach_Info/Approach_Angle_Max, >= min
    std::optional<float> approachDist;                                    // Approach_Info/Approach_Dist, spec range [0,150]
    std::optional<float> launchDistMinSquared;                              // Approach_Info/Launch_Dist_Min, >=0, STORED SQUARED
    // Target_Info's children are positional 4-tuples (Target_Type, Radius_Min,
    // Radius_Max, Target_FOV) - the same positional convention weapons.xtbl's
    // Effect_Situations uses (spec section 2.3); this reader groups Target_Info's
    // children into consecutive 4s in document order rather than by tag name.
    ContinuousExplosionTargetInfo targetDefault;      // Target_Type == "default"; effectively required
    ContinuousExplosionTargetInfo targetInCar;          // Target_Type == "in_car"
    ContinuousExplosionTargetInfo targetInAircraft;       // Target_Type == "in_aircraft"
    std::optional<std::string> whizSound;                   // Audio_Info/Whiz_Sound (Wwise name text); 0 if absent
    std::optional<float> avgLength;                            // Audio_Info/Avg_Length (i), default 0
};

ContinuousExplosion ParseContinuousExplosion(const Node* row);
std::vector<ContinuousExplosion> ParseAllContinuousExplosions(const Document& doc);

// ===========================================================================
// 11.1 combat_actions.xtbl (spec section 11.1)
// ===========================================================================
inline constexpr std::string_view kCombatActionPreconditionFlagNames[5] = {
    "on foot", "in vehicle", "in water", "must have target", "can do in cover",
};
// The 70 built-in action names (spec section 11.1) a row's Name is matched
// against; a row naming anything else sets a global flag and is ignored by
// the real loader. Provided for validators; this table adds NO new actions.
inline constexpr std::string_view kCombatActionBuiltinNames[70] = {
    "idle",
    "brute throw prop", "brute car flip", "brute bull rush", "brute qte player", "brute attack brute",
    "brute gun finisher", "avatar shockwave", "avatar stomp", "avatar fireballs", "avatar teleport stomp", "avatar bullrush",
    "hold position", "melee stand back", "move retreat", "move regroup", "move advance", "move chase",
    "avatar chase", "move surround", "move rush", "move rush to melee", "move scripted", "move to navmesh",
    "move sidestep", "move to post combat", "move to post idle", "move close in", "killbane chase", "killbane strafe",
    "quick kill",
    "cover popout",
    "cover take def", "cover take off", "cover advance", "cover fire", "cover stay down",
    "fire", "fire sweep", "fire suppress", "fire chaos",
    "follow make way", "follow avoid LOF", "follow formation", "follow acquire", "follow player",
    "follow teleport", "follow lemming", "follow to car", "follow other car", "follow browse",
    "throw grenade",
    "follow carjack", "vehicle enter scpt", "vehicle extract",
    "human shield grab",
    "investigate move", "investigate look", "investigate peek",
    "melee", "melee vehicle",
    "vehicle passenger",
    "pickup weapon",
    "reload",
    "roller blader change",
    "taunt", "avatar taunt",
    "throw weapon",
    "zombie eat", "zombie explode",
};

struct CombatActionOverrideRow {
    std::string name;                     // matched case-insensitively against kCombatActionBuiltinNames; unmatched names are otherwise ignored by the real loader
    Always<uint16_t> minRepeatTimeMs;        // Min_repeat_time (a, s32 truncated to u16); built-in static default 0
    Always<uint16_t> autoAbortMs;              // Auto_abort_ms; built-in static default 15000
    Always<uint16_t> noInterruptMs;              // No_interrupt_ms; built-in static default 0
    Always<uint16_t> refreshDesireIntervalMs;      // refresh_desire_interval; built-in static default 300; -1 stored as 0xFFFF
    uint8_t preconditionFlags = 0;                   // precondition/pre_flags/Flag bitmask (5 literals, contiguous)
};

CombatActionOverrideRow ParseCombatActionOverride(const Node* row);
std::vector<CombatActionOverrideRow> ParseAllCombatActionOverrides(const Document& doc);

// ===========================================================================
// 11.2 combat_tricks.xtbl (spec section 11.2)
// ===========================================================================
inline constexpr std::string_view kCombatTrickNames[18] = {
    "Gang_Kill", "Gang_Vehicle_Kill", "One_Hit_Kill", "Human_Shield_Kill", "Head_Shot_Kill", "Nut_Shot_Kill",
    "Throwing", "Multi_Kill", "Specialist_Kill", "Brute_Beat_Kill", "Brute_Kill", "STAG_Kill", "Explosive_Kill",
    "Testicular_Assault", "Sprint_Attack", "STAG_Vehicle_Kill", "Heli_Or_Vtol_Kill", "Tank_Kill",
};

struct CombatTrickDescriptor {
    bool present = false;                      // whether this trick's own child element existed
    std::optional<int32_t> durationMs;            // Duration, seconds -> ms (i); documented default = the row's Default_Duration
    Always<int32_t> maxRespect;                      // Max_Respect (a)
    Always<int32_t> maxLifetimeRespect;                // Max_Lifetime_Respect (a)
    Always<float> maxCash;                               // Max_Cash (a)
    Always<float> minValue;                                // Min_Value (a); meaning not traced by the spec (HYPOTHESIS)
    Always<float> maxValue;                                  // Max_Value (a); meaning not traced by the spec (HYPOTHESIS)
    std::optional<int32_t> killDurationMs;                     // Kill_Duration, seconds -> ms; Multi_Kill only
};

struct CombatTricksRow {
    Always<float> defaultDurationMs;             // Default_Duration (a); documented: used only if > 0, else 5000ms
    std::optional<float> recordDisplayTimeMs;      // Record_Display_Time (i), clamped >= 0, ms; default 0
    std::optional<float> recordQueueTimeMs;          // Record_Queue_Time (i), clamped >= 0, ms; default 0
    Always<float> recordThreshold;                     // Record_Threshold (a), clamped >= 0, stored /100
    std::array<CombatTrickDescriptor, 18> tricks;         // indexed by kCombatTrickNames (trick id == index)
};

// Parses the file's single `Combat_Tricks` row (the first one, per spec).
CombatTricksRow ParseCombatTricks(const Document& doc);

// ===========================================================================
// 12.1 weapon_tracers.xtbl (spec section 12.1)
// ===========================================================================
struct WeaponTracerRecord {
    std::string name;
    std::optional<std::string> effect;         // Effect (name text); a row without Effect passes NULL to the resolver
    std::optional<std::string> emitterEffect;    // Emitter_Effect (name text); -1 if absent
    Always<int32_t> maxParticles;                  // Max_Particles (a)
    Always<float> lifetime;                          // Lifetime (a)
    std::optional<float> ricochetChance;                // Ricochet/Chance (i); 0 if Ricochet absent. NOTE real DLC data sometimes
                                                          // places <Chance> directly under the row - the reader only looks inside
                                                          // Ricochet (spec section 12.1); that shallow placement is NOT read here either.
    std::optional<float> ricochetDistance;                 // Ricochet/Distance (i); 0 if Ricochet absent
    Always<float> velocityScale;                             // Velocity_Scale (a)
    Always<float> ricochetVelocityScale;                       // Ricochet/Ricochet_velocity_scale (a, inside Ricochet); 0 if Ricochet absent
    std::optional<float> hotLengthSize;                          // Hot_Length_Size (i); default -1.0
};

WeaponTracerRecord ParseWeaponTracer(const Node* row);
std::vector<WeaponTracerRecord> ParseAllWeaponTracers(const Document& doc);

// ===========================================================================
// 12.2 weapon_tracer_materials.xtbl (spec section 12.2)
// ===========================================================================
// [NOTE - spec-tables-weapons-combat.md 1.6 (`FUN_006F76F0` row) now lists the 33 names by cross-reference
// to spec-tables-environment.md 11.6 `bitmap_materials.xtbl` [CONFIRMED - disassembly]: slot 0..32, `not set` = 31,
// `_stricmp`, unknown -> 31. The earlier "never enumerated" wording below pre-dates that cross-reference. Still kept
// as raw text (label/note only, no behaviour change; resolution to a slot index is out of scope here).]
struct TracerMaterialRow {
    std::string materialName;    // Name: a physical-material name; the 33-name resolution table (FUN_006F76F0) is
                                    // never enumerated in this spec (only referenced, section 1.6) - kept as raw text;
                                    // real engine behaviour: an unmatched name silently overwrites slot 31
    Always<float> tracerDampener;  // Tracer_Dampener (a); spec notes the always-reader quirk overwrites the engine's 1.0 preset when absent
};

TracerMaterialRow ParseTracerMaterial(const Node* row);
std::vector<TracerMaterialRow> ParseAllTracerMaterials(const Document& doc);

// ===========================================================================
// 13.1 crib_weapons.xtbl (spec section 13.1)
// ===========================================================================
struct CribWeaponEntry {
    std::optional<std::string> weapon;  // Weapons_List/Entry/Weapon (name text; resolved via FUN_00B81220 WITHOUT a null
                                           // check by the real loader - an unknown name would fault there; out of scope here)
    Always<bool> unlocked;                 // Entry/Unlocked (a)
};

// Walks Crib_Weapons/Weapons_List/Entry directly (there is exactly one Crib_Weapons row per file).
std::vector<CribWeaponEntry> ParseCribWeapons(const Document& doc);

// ===========================================================================
// 13.2 store_weapons.xtbl (spec section 13.2)
// ===========================================================================
struct StoreWeaponEntry {
    std::optional<std::string> mission;   // Entry/Mission (name text; resolver out of scope)
    std::optional<std::string> weapon;      // Entry/Weapon (name text)
    Always<bool> unlocked;                    // Entry/Unlocked (a)
    // Num_Hoods/Angle/Distance/Offset are declared elements this row can
    // carry but the real loader (FUN_00817F90) does NOT read them (spec
    // section 13.2, OPEN: consumer not found) - exposed here as dead data.
    std::optional<int32_t> numHoods;   // Entry/Num_Hoods
    std::optional<float> angle;          // Entry/Angle
    std::optional<float> distance;         // Entry/Distance
    std::optional<float> offset;             // Entry/Offset
};

// Walks Store_Weapons/Weapons_List/Entry directly.
std::vector<StoreWeaponEntry> ParseStoreWeapons(const Document& doc);

// ===========================================================================
// 14.1 strafe_angles.xtbl (spec section 14.1)
// ===========================================================================
struct StrafeAnglesRow {
    std::string name;                     // bounded copy, 0x40 bytes in the engine; kept in full here
    Always<float> forwardRadians;            // Forward, degrees -> radians
    Always<float> rightRadians;                // Right
    Always<float> backwardRightRadians;          // Backward_Right
    Always<float> backwardRadians;                 // Backward
    Always<float> backwardLeftRadians;               // Backward_Left
    Always<float> leftRadians;                         // Left
    bool useTurnLimits = false;                          // Use_Turn_Limits present
    Always<float> turnLimitLeftRadians;                    // Use_Turn_Limits/Left_Limit: stored as -(360-value) degrees -> radians; 0 if absent
    Always<float> turnLimitRightRadians;                     // Use_Turn_Limits/Right_Limit, degrees -> radians; 0 if absent
};

StrafeAnglesRow ParseStrafeAngles(const Node* row);
std::vector<StrafeAnglesRow> ParseAllStrafeAngles(const Document& doc);

// ===========================================================================
// 14.2 taunting.xtbl (spec section 14.2) - single `Taunting` row
// ===========================================================================
struct TauntingRow {
    Always<int32_t> maxTaunts;         // Max_Taunts (a); documented: both become 0 if Max<1 or Max<Min (not applied here)
    Always<int32_t> minTaunts;           // Min_Taunts (a)
    Always<int32_t> aggroTauntValue;       // Aggro_Taunt_Value (a); documented minimum 1 (not applied here)
    Always<int32_t> deathTauntValue;         // Death_Taunt_Value (a); documented minimum 1
    Always<float> deathTauntDelayMs;           // Death_Taunt_Delay, seconds >= 0 -> ms
    Always<float> maxTimeBetweenTauntsMs;        // Max_Time_Between_Taunts, seconds >= 0 -> ms
    Always<float> deathTauntDistanceSquared;       // Death_Taunt_Distance, f32 >= 0, STORED SQUARED
    Always<float> tauntCompletPercent;               // Taunt_Complet_Percent (sic - misspelled in the reader itself), clamped [0,100], stored /100
    Always<float> awardMultiplier;                     // Award_Multiplier, documented minimum 1.0
    Always<float> firstTauntCash;                        // First_Taunt_Cash, documented minimum 0
    Always<int32_t> firstTauntRespect;                     // First_Taunt_Respect, documented minimum 0
    Always<int32_t> maxLifetimeRespect;                      // Max_Lifetime_Respect, documented minimum 0
};

TauntingRow ParseTaunting(const Document& doc);

// ===========================================================================
// 14.3 hostage.xtbl (spec section 14.3)
// ===========================================================================
inline constexpr std::string_view kHostageVehicleClassNames[6] = {"Compact", "Sedan", "Luxury", "Exotic", "SUV", "Truck"};

struct HostageDifficultyLevel {
    std::optional<int32_t> numHostages;    // must be 1, 2 or 3 (else the level is ignored by the real loader; not enforced here)
    std::optional<float> minEvasionTimeMs;   // Min_Evasion_Time, seconds >= 0 -> ms
    std::optional<float> maxEvasionTimeMs;     // Max_Evasion_Time, seconds, raised to >= min -> ms (raising not applied here)
    std::optional<int32_t> notorietyPerSec;      // Notoriety_Per_Sec; documented: negative -> 0 (not applied here)
    std::optional<int32_t> respect;                // Respect; documented: negative -> 0
    std::optional<float> cash;                       // Cash; documented: negative -> 0
};

struct HostageVehicleClassRow {
    int classIndex = -1;                                     // Class_Name, 6-name enum (kHostageVehicleClassNames)
    std::vector<HostageDifficultyLevel> difficultyLevels;       // Difficulty_Levels/Difficulty_Level
};

struct HostageRow {
    std::optional<int32_t> minNotoriety;         // Min_Notoriety; documented: outside 0..5 becomes 3 (not applied here)
    std::optional<int32_t> maxLifetimeRespect;     // Max_Lifetime_Respect, documented minimum 0
    std::vector<HostageVehicleClassRow> vehicleClasses;  // Vehicle_Classes/Vehicle_Class
};

HostageRow ParseHostage(const Document& doc);

// ===========================================================================
// 14.4 windshield_cannon.xtbl (spec section 14.4) - single `Windshield_Cannon` row
// ===========================================================================
struct WindshieldCannonRow {
    Always<float> maxDistance;              // Max_Distance (a); documented: both forced 0 if Max<=0 or Max<Min (not applied here)
    Always<float> minDistance;                // Min_Distance (a)
    Always<float> recordThreshold;              // Record_Threshold (a), documented >= 0, stored /100
    std::optional<float> recordDisplayTimeMs;     // Record_Display_Time (i), >= 0, seconds -> ms, default 0
    std::optional<float> recordQueueTimeMs;         // Record_Queue_Time (i), >= 0, seconds -> ms, default 0
    Always<int32_t> maxRespect;                       // Max_Respect (a), documented >= 0
    Always<int32_t> maxLifetimeRespect;                 // Max_Lifetime_Respect (a), documented >= 0
    Always<float> maxCash;                                // Max_Cash (a), documented >= 0
};

WindshieldCannonRow ParseWindshieldCannon(const Document& doc);

}  // namespace sr3tables_weapons
