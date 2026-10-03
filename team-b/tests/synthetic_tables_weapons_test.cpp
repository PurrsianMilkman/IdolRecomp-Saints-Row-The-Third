// Synthetic tests for sr3tables_weapons (spec-tables-weapons-combat.md).
// Every fixture below is a hand-built XML string transcribed from the SPEC
// TEXT (section numbers cited per test), never derived from this project's
// own reader source - a pass here proves the reader implements what the
// spec says, not that the spec is right. The real-data cross-check is
// tools/validation/validate_tables_weapons_population.cpp.
//
// Style follows this project's synthetic-suite convention (see
// tests/synthetic_xtbl_test.cpp, tests/synthetic_effects_test.cpp): a tiny
// hand-rolled CHECK macro, a running pass count, "<N> tests passed." on the
// last line.
//
// Mutation-check note (this project's process, measurement-discipline.md):
// at least 3 of the CHECKs below were confirmed to actually catch a broken
// reader by hand, before this file was finalised:
//   1. testWeaponNormalRow's Fire_Cone_Angle check: temporarily changed the
//      cos() half-angle formula in tables_weapons.cpp to use the FULL angle
//      (dropped the "* 0.5f") -> the check
//      `near(w.fireConeAngleCos.value, std::cos(30.0f * kDegToRad * 0.5f), ...)`
//      failed as expected; reverted.
//   2. testWeaponFlagsAndSpecialCases's gap-bit check: temporarily removed
//      the "" placeholder at index 27 of kFlagsAWordNames (shifted a real
//      name into it) -> `CHECK((w.flagsA & 0x8000000u) == 0)` failed
//      (a text match now landed on the gap bit); reverted.
//   3. testMeleeAttackLimbBoundary's absent-vs-unmatched check: temporarily
//      made ParseMeleeMove return -1 (instead of 0) when <Impact> itself is
//      absent -> `CHECK(mv.attackLimb == 0)` failed as expected; reverted.

#include <cmath>
#include <iostream>
#include <string>

#include "sr3tables_weapons/combat.h"
#include "sr3tables_weapons/weapons.h"
#include "sr3xtbl/xtbl.h"

namespace {

int g_passed = 0;
int g_failed = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (cond) {                                                                     \
            ++g_passed;                                                                 \
        } else {                                                                        \
            ++g_failed;                                                                 \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__ << "\n"; \
        }                                                                               \
    } while (0)

using namespace sr3xtbl;
using namespace sr3tables_weapons;

bool near(float a, float b, float tol) { return (a > b ? a - b : b - a) <= tol; }

Document P(std::string_view s) { return ParseDocument(s); }

// ---------------------------------------------------------------------------
// weapons.xtbl (spec section 2) - the flagship table, given its size.
// ---------------------------------------------------------------------------
void testWeaponNormalRow() {
    // A "kitchen sink" row exercising most of section 2.2/2.3's sub-blocks,
    // transcribed field-by-field from the spec's own element names.
    const char* xml =
        "<root><Table><Weapon>"
        "<Name>sp_test01_w</Name>"
        "<Framework>main</Framework>"
        "<Is_DLC>False</Is_DLC>"
        "<Flags>"
        "<Flag>unlimited ammo</Flag>"          // word A bit 0x8
        "<Flag>left hand</Flag>"                // word B bit 0x800
        "<Flag>manned turret</Flag>"             // word C bit 0x2
        "</Flags>"
        "<Special_Case_Type>Wieldable Prop Weapon</Special_Case_Type>"
        "<Melee_Damage_Overrides><NPC>1.5</NPC><Player>2.5</Player><Online>3.5</Online></Melee_Damage_Overrides>"
        "<Animation_Group>rifle_std</Animation_Group>"
        "<Grenade_Type>flashbang</Grenade_Type>"
        "<Weapon_Class>rifle</Weapon_Class>"
        "<Category>WPNCAT_RIFLE</Category>"
        "<Inv_Slot>rifle</Inv_Slot>"
        "<Tracer_Info><Tracer>rifle_tracer</Tracer><Tracer_Frequency>3</Tracer_Frequency></Tracer_Info>"
        "<Constant_Effects>"
        "<Constant_Effect><Effect>fx_a</Effect><Condition>during melee swing</Condition></Constant_Effect>"
        "<Constant_Effect><Effect>fx_b</Effect><Condition>fine aim</Condition></Constant_Effect>"
        "<Constant_Effect><Effect>fx_c</Effect><Condition>weapon raised</Condition></Constant_Effect>"
        "<Constant_Effect><Effect>fx_d</Effect><Condition>always on</Condition></Constant_Effect>"
        "</Constant_Effects>"
        "<Audio><Weapon_Model>Rifle</Weapon_Model><looping>True</looping></Audio>"
        "<Target_Lockon><Lockon_Time_MS>500</Lockon_Time_MS><Locking_Rotation_Furthest_Angle>90</Locking_Rotation_Furthest_Angle>"
        "<flags><Flag>enemy aircraft only</Flag></flags></Target_Lockon>"
        "<Trigger_Type>automatic</Trigger_Type>"
        "<Ammo>Bullet_Rifle</Ammo>"
        "<Magazine_Size>30</Magazine_Size>"
        "<Range_Max>500.0</Range_Max>"
        "<Time_Management><Refire_Delay>100</Refire_Delay>"
        "<npc_refire_delay><min>6000</min><max>3000</max><npc_refire_type>1. Random Range</npc_refire_type></npc_refire_delay>"
        "</Time_Management>"
        "<Damage_Max><NPC_Damage>10</NPC_Damage><Player_Damage>8</Player_Damage></Damage_Max>"
        "<Damage_Min><NPC_Damage>5</NPC_Damage><Player_Damage>4</Player_Damage></Damage_Min>"
        "<Explosion>expl_a</Explosion>"
        "<Wieldable_Prop_Death_VFX>fx_death</Wieldable_Prop_Death_VFX>"
        "<Wieldable_Prop_Hits_Allowed>3</Wieldable_Prop_Hits_Allowed>"
        "<Fire_Cone_Angle>30</Fire_Cone_Angle>"
        "<Fire_Cone_Metrics><Metric_Type><Min_Max><Near_Radius>1.0</Near_Radius><Far_Radius>2.0</Far_Radius></Min_Max></Metric_Type></Fire_Cone_Metrics>"
        "<PlayerWeaponSpread><SpreadMinMax><Player_Spread_Min>1</Player_Spread_Min><Player_Spread_Max>2</Player_Spread_Max></SpreadMinMax>"
        "<SpreadMovementMultipliers><Movement_Multiplier_Crouch>0.5</Movement_Multiplier_Crouch></SpreadMovementMultipliers></PlayerWeaponSpread>"
        "<NPCWeaponSpread><SpreadMinMax><NPC_Spread_Min>1</NPC_Spread_Min><NPC_Spread_Max>2</NPC_Spread_Max>"
        "<SpreadTargetMovementMultipliers><Target_Movement_Multiplier_Walk>0.75</Target_Movement_Multiplier_Walk></SpreadTargetMovementMultipliers>"
        "</SpreadMinMax></NPCWeaponSpread>"
        "<Ragdoll_Info><Chance>0.5</Chance><Death_Range_Max>10</Death_Range_Max></Ragdoll_Info>"
        "<Projectile_Info><Model>proj_model</Model><Speed>50</Speed><Gravity>-9.8</Gravity>"
        "<Fuse_Time>2000</Fuse_Time><Angular_Velocity><X>1</X><Y>2</Y><Z>3</Z></Angular_Velocity>"
        "<Projectile_Flags><Flag>sticky</Flag><Flag>swarm</Flag></Projectile_Flags></Projectile_Info>"
        "<melee_material_effects>"
        "<material_effect><material>concrete</material><effect>spark_concrete</effect></material_effect>"
        "<material_effect><material>metal</material><effect>spark_metal</effect></material_effect>"
        "</melee_material_effects>"
        "<Waterspray_info><Water_stream_force>1</Water_stream_force><Pressure_Increase_Rate>2</Pressure_Increase_Rate>"
        "<Pressure_Decrease_Rate>3</Pressure_Decrease_Rate><Pressure_Restore_Time>4</Pressure_Restore_Time></Waterspray_info>"
        "<Charge_Release_Info><Charge_Time_sec>2.0</Charge_Time_sec><Charge_Flags><Flag>Show HUD on charge</Flag></Charge_Flags></Charge_Release_Info>"
        "<Camera_Info><Primary_Fire_Camera_Shake>shake_a</Primary_Fire_Camera_Shake><Primary_Fire_Camera_Shake_Intensity>2.0</Primary_Fire_Camera_Shake_Intensity></Camera_Info>"
        "<Overheat_Info><Percent_Increase_Per_Shot>5</Percent_Increase_Per_Shot><Overheat_Flags><Flag>applies to primary</Flag><Flag>play reload anim</Flag></Overheat_Flags></Overheat_Info>"
        "<Effect_Situations>"
        "<Situation><Situation>muzzle flash</Situation><Effect>fx_muzzle</Effect></Situation>"
        "<Situation><Situation>tracer</Situation><Effect>fx_tracer</Effect></Situation>"
        "<Situation><Situation>overheat</Situation><Effect>fx_overheat</Effect></Situation>"
        "<Situation><Situation>genki fire</Situation><Effect>fx_genki</Effect></Situation>"
        "<Situation><Situation>projectile</Situation><Effect>fx_fifth_ignored</Effect></Situation>"  // 5th: must be ignored
        "</Effect_Situations>"
        "<Burst_Fire_Info><Shots>3</Shots><Burst_Delay_ms>50</Burst_Delay_ms></Burst_Fire_Info>"
        "<NPC_Desired_Burst_Size>4</NPC_Desired_Burst_Size>"
        "<Max_Melee_Impacts>7</Max_Melee_Impacts>"
        "<Blood_Decal_Scale>2.0</Blood_Decal_Scale>"
        "</Weapon>"
        "</Table></root>";
    Document doc = P(xml);
    CHECK(doc.warnings().empty());
    const Node* row = FindChild(doc.table(), "Weapon");
    CHECK(row != nullptr);
    Weapon w = ParseWeapon(row);

    CHECK(w.name == "sp_test01_w");
    CHECK(w.framework.has_value() && *w.framework == "main");
    CHECK(w.isDlc.has_value() && *w.isDlc == false);

    // Flags: word A bit 0x8 (unlimited ammo), word B bit 0x800 (left hand), word C bit 0x2 (manned turret).
    CHECK((w.flagsA & 0x8u) != 0);
    CHECK((w.flagsB & 0x800u) != 0);
    CHECK((w.flagsC & 0x2u) != 0);

    CHECK(w.specialCaseType == 8);  // "Wieldable Prop Weapon" is index 8 (spec section 2.5)
    CHECK(w.wieldablePropDeathVfx.has_value() && *w.wieldablePropDeathVfx == "fx_death");
    CHECK(w.wieldablePropHitsAllowed.has_value() && *w.wieldablePropHitsAllowed == 3);

    CHECK(w.meleeDamageOverrides.present);
    CHECK(w.meleeDamageOverrides.npc.present && w.meleeDamageOverrides.npc.value == 1.5f);
    CHECK(w.meleeDamageOverrides.player.value == 2.5f && w.meleeDamageOverrides.online.value == 3.5f);

    CHECK(w.animationGroup.has_value() && *w.animationGroup == "rifle_std");
    CHECK(w.grenadeType == 2);   // "flashbang" is index 2 (standard=0,stun=1,flashbang=2,molotov=3)
    CHECK(w.weaponClass == 2);   // "rifle" is index 2
    CHECK(w.category.has_value() && *w.category == 4);  // WPNCAT_RIFLE is index 4
    CHECK(w.invSlot == 5);       // "rifle" is index 5 of the 11-name Inv_Slot table

    CHECK(w.tracerInfo.present);
    CHECK(w.tracerInfo.tracer.has_value() && *w.tracerInfo.tracer == "rifle_tracer");
    CHECK(w.tracerInfo.tracerFrequency.present && w.tracerInfo.tracerFrequency.value == 3);

    // Constant_Effect/Condition mapping (spec section 2.2): the 4th uses the
    // real DLC3 unrecognised value "always on", which must map to 0 - NOT -1.
    CHECK(w.constantEffects.size() == 4);
    CHECK(w.constantEffects[0].condition == 1);
    CHECK(w.constantEffects[1].condition == 2);
    CHECK(w.constantEffects[2].condition == 3);
    CHECK(w.constantEffects[3].condition == 0);

    CHECK(w.audio.present && w.audio.weaponModel.has_value() && *w.audio.weaponModel == "Rifle");
    CHECK(w.audio.looping.has_value() && *w.audio.looping == true);

    CHECK(w.targetLockon.present);
    CHECK(w.targetLockon.lockonTimeMs.present && w.targetLockon.lockonTimeMs.value == 500);
    CHECK(near(w.targetLockon.lockingRotationFurthestAngleRadians.value, 90.0f * kDegToRad, 1e-5f));
    CHECK(w.targetLockon.enemyAircraftOnly);

    CHECK(w.triggerType == 2);  // "automatic"
    CHECK(w.ammo.has_value() && *w.ammo == "Bullet_Rifle");
    CHECK(w.magazineSize.present && w.magazineSize.value == 30);
    CHECK(w.rangeMax.present && w.rangeMax.value == 500.0f);

    // Time_Management: the real DLC data's min(6000) > max(3000) quirk (spec
    // section 2.3) - this reader stores the RAW parsed values (the engine's
    // "raise max to at least min" correction is documented but not applied
    // here, since it is a runtime rule, not schema).
    CHECK(w.timeManagement.refireDelayMs.value == 100);
    CHECK(w.timeManagement.npcRefireDelayMin.value == 6000);
    CHECK(w.timeManagement.npcRefireDelayMax.value == 3000);
    CHECK(w.timeManagement.npcRefireType == 0);  // "1. Random Range" -> '1'-'1' == 0

    CHECK(w.damageMaxNpc.value == 10.0f && w.damageMaxPlayer.value == 8.0f);
    CHECK(w.damageMinPresent);
    CHECK(w.damageMinNpc.value == 5.0f && w.damageMinPlayer.value == 4.0f);

    CHECK(w.explosion.has_value() && *w.explosion == "expl_a");
    CHECK(!w.altExplosion.has_value());  // not authored - defaulting to Explosion is a caller concern, not invented here

    // Fire_Cone_Angle: stored value is cos(half the authored degrees) (spec section 2.2).
    CHECK(w.fireConeAngleCos.present);
    CHECK(near(w.fireConeAngleCos.value, std::cos(30.0f * 0.5f * kDegToRad), 1e-6f));

    CHECK(w.fireConeMetrics.present && w.fireConeMetrics.isMinMaxForm);
    CHECK(w.fireConeMetrics.nearRadius.has_value() && *w.fireConeMetrics.nearRadius == 1.0f);
    CHECK(w.fireConeMetrics.farRadius.has_value() && *w.fireConeMetrics.farRadius == 2.0f);

    CHECK(near(w.playerWeaponSpread.spreadMinRadians.value, 1.0f * kDegToRad, 1e-6f));
    CHECK(w.playerWeaponSpread.movementMultipliers.crouch.has_value() && *w.playerWeaponSpread.movementMultipliers.crouch == 0.5f);
    // Every multiplier is "if present" - one not authored here (walk) must be nullopt, not a defaulted 1.0.
    CHECK(!w.playerWeaponSpread.movementMultipliers.walk.has_value());

    CHECK(w.npcWeaponSpread.targetMovementMultipliers.walk.has_value() && *w.npcWeaponSpread.targetMovementMultipliers.walk == 0.75f);

    CHECK(w.ragdollInfo.chance.value == 0.5f && w.ragdollInfo.deathRangeMax.value == 10.0f);
    CHECK(w.ragdollInfo.deathVelocityHorizontal.present == false);  // not authored: Always<T> stays !present

    CHECK(w.projectileInfo.present);
    CHECK(w.projectileInfo.model.has_value() && *w.projectileInfo.model == "proj_model");
    CHECK(w.projectileInfo.speed.value == 50.0f);
    CHECK(w.projectileInfo.fuseTimeMs.present && w.projectileInfo.fuseTimeMs.value == 2000);
    CHECK(w.projectileInfo.angularVelocity.complete());
    CHECK(w.projectileInfo.angularVelocity.value().x == 1.0f && w.projectileInfo.angularVelocity.value().z == 3.0f);
    CHECK((w.projectileInfo.projectileFlags & 0x4u) != 0);       // "sticky"
    CHECK((w.projectileInfo.projectileFlags & 0x20000u) != 0);   // "swarm"
    CHECK((w.projectileInfo.projectileFlags & 0x10000u) != 0);   // Fuse_Time presence sets this bit (spec section 2.3)

    CHECK(w.meleeMaterialEffects.size() == 2);
    CHECK(w.meleeMaterialEffects[0].first == "concrete" && w.meleeMaterialEffects[0].second.value() == "spark_concrete");

    CHECK(w.watersprayInfo.waterStreamForce.value == 1.0f);
    CHECK(w.watersprayInfo.pressureIncreaseRate.has_value() && *w.watersprayInfo.pressureIncreaseRate == 2.0f);
    CHECK(w.watersprayInfo.pressureDecreaseRate.has_value() && *w.watersprayInfo.pressureDecreaseRate == 3.0f);

    // Charge_Release_Info reader flavours (spec section 2.3, CORRECTED
    // 2026-10-02): Charge_Time_sec is "always" (Always<float>, not
    // optional<float>); the row here authors only Charge_Time_sec and
    // Charge_Flags, so the four "if present" scalars (Min_Charge_Percent,
    // Charge_Base, Min_Range, Pre_Charge_Delay) must read back absent, not a
    // fabricated default, and Auto_Release ("always" bool, also unauthored
    // here) must read back present=false.
    CHECK(w.chargeReleaseInfo.chargeTimeReciprocal.present && near(w.chargeReleaseInfo.chargeTimeReciprocal.value, 0.5f, 1e-6f));
    CHECK(!w.chargeReleaseInfo.minChargePercent.has_value());
    CHECK(!w.chargeReleaseInfo.chargeBase.has_value());
    CHECK(!w.chargeReleaseInfo.minRange.has_value());
    CHECK(!w.chargeReleaseInfo.preChargeDelay.has_value());
    CHECK(!w.chargeReleaseInfo.autoRelease.present);
    CHECK(w.chargeReleaseInfo.showHudOnCharge);

    CHECK(w.cameraInfo.primaryFire.shakeName.has_value() && *w.cameraInfo.primaryFire.shakeName == "shake_a");
    CHECK(w.cameraInfo.primaryFire.intensity.value == 2.0f);

    CHECK((w.overheatInfo.overheatFlags & 0x1u) != 0);  // "applies to primary"
    CHECK((w.overheatInfo.overheatFlags & 0x4u) != 0);  // "play reload anim"

    // Effect_Situations: positional, capacity 4, a fifth child ignored (spec section 2.3).
    CHECK(w.effectSituations.size() == 4);
    CHECK(w.effectSituations[0].situation == 0 && w.effectSituations[0].effect.value() == "fx_muzzle");
    CHECK(w.effectSituations[1].situation == 2);  // "tracer"
    CHECK(w.effectSituations[3].situation == 24 && w.effectSituations[3].effect.value() == "fx_genki");

    CHECK(w.burstFireInfo.shots.value == 3 && w.burstFireInfo.burstDelayMs.value == 50);
    // NPC_Desired_Burst_Size is only read when Trigger_Type == automatic (2) - it is here.
    CHECK(w.npcDesiredBurstSize.has_value() && *w.npcDesiredBurstSize == 4);

    CHECK(w.maxMeleeImpacts.has_value() && *w.maxMeleeImpacts == 7);
    CHECK(w.bloodDecalScale.has_value() && *w.bloodDecalScale == 2.0f);
}

// ---------------------------------------------------------------------------
// A row missing optional elements: every "if present" reader must report
// absence (nullopt / present=false), never a silently invented default.
// ---------------------------------------------------------------------------
void testWeaponMissingOptional() {
    const char* xml = "<root><Table><Weapon><Name>bare_w</Name></Weapon></Table></root>";
    Document doc = P(xml);
    const Node* row = FindChild(doc.table(), "Weapon");
    Weapon w = ParseWeapon(row);

    CHECK(w.name == "bare_w");
    CHECK(!w.framework.has_value());
    CHECK(!w.isDlc.has_value());
    CHECK(w.flagsA == 0 && w.flagsB == 0 && w.flagsC == 0);
    CHECK(w.specialCaseType == -1);                 // absent -> -1 (spec section 2.2)
    CHECK(!w.meleeDamageOverrides.present);
    CHECK(!w.animationGroup.has_value());             // required by the real loader, but this reader surfaces absence rather than fabricating a name
    CHECK(!w.category.has_value());                     // "written only if present" - absent means no value stored here (engine default 0 is a caller concern)
    CHECK(w.invSlot == 0xFF);                             // absent -> 0xFF (spec section 2.2)
    CHECK(w.weaponClass == -1 && w.grenadeType == -1 && w.triggerType == -1 && w.altTriggerType == -1);
    CHECK(!w.tracerInfo.present);
    CHECK(w.constantEffects.empty());
    CHECK(!w.audio.present);
    CHECK(!w.targetLockon.present);
    CHECK(!w.damageMinPresent);
    CHECK(!w.fireConeAngleCos.present);               // Always<T>: absent means present=false, value is NOT the documented 1.0 default
    CHECK(!w.fireConeMetrics.present);
    CHECK(!w.projectileInfo.present);
    CHECK(w.meleeMaterialEffects.empty());
    CHECK(!w.vehicleWeapon.present && w.vehicleWeapon.primaryWeapons.empty());
    CHECK(w.effectSituations.empty());
    CHECK(!w.maxMeleeImpacts.has_value());
    CHECK(!w.bloodDecalScale.has_value());
    CHECK(!w.wieldablePropDeathVfx.has_value());  // Special_Case_Type != 8, so this reader does not even look
}

// ---------------------------------------------------------------------------
// Flags word gaps and the two name-based special cases (spec section 2.4).
// ---------------------------------------------------------------------------
void testWeaponFlagsAndSpecialCases() {
    // sp_gat01_w gets flag-A bit 0x8 (unlimited ammo) BY NAME, with no <Flags> at all.
    {
        Document doc = P("<root><Table><Weapon><Name>sp_gat01_w</Name></Weapon></Table></root>");
        Weapon w = ParseWeapon(FindChild(doc.table(), "Weapon"));
        CHECK((w.flagsA & 0x8u) != 0);
    }
    // A different name does NOT get the bit even with an empty Flags block.
    {
        Document doc = P("<root><Table><Weapon><Name>sp_gat02_w</Name><Flags/></Weapon></Table></root>");
        Weapon w = ParseWeapon(FindChild(doc.table(), "Weapon"));
        CHECK((w.flagsA & 0x8u) == 0);
    }
    // Gap bits (0x8000000 and 0x80000000 of word A) cannot be set by ANY Flag
    // text - they are set only by Waterspray_info / crib_weapons.xtbl.
    {
        Document doc = P(
            "<root><Table><Weapon><Name>gap_test_w</Name>"
            "<Flags><Flag>this text matches nothing</Flag><Flag></Flag></Flags>"
            "</Weapon></Table></root>");
        Weapon w = ParseWeapon(FindChild(doc.table(), "Weapon"));
        CHECK((w.flagsA & 0x8000000u) == 0);
        CHECK((w.flagsA & 0x80000000u) == 0);
    }
    // Category: "written only if present" (spec section 2.2) - three distinct
    // observable states: absent, present-and-matched, present-and-unmatched.
    {
        Document a = P("<root><Table><Weapon><Name>c1</Name></Weapon></Table></root>");
        CHECK(!ParseWeapon(FindChild(a.table(), "Weapon")).category.has_value());
        Document b = P("<root><Table><Weapon><Name>c2</Name><Category>WPNCAT_SHOTGUN</Category></Weapon></Table></root>");
        Weapon wb = ParseWeapon(FindChild(b.table(), "Weapon"));
        CHECK(wb.category.has_value() && *wb.category == 3);
        Document c = P("<root><Table><Weapon><Name>c3</Name><Category>NoSuchCategory</Category></Weapon></Table></root>");
        Weapon wc = ParseWeapon(FindChild(c.table(), "Weapon"));
        CHECK(wc.category.has_value() && *wc.category == -1);  // present but unmatched: element WAS present, index is -1
    }
}

// ---------------------------------------------------------------------------
// Charge_Release_Info reader flavours (spec section 2.3, CORRECTED 2026-10-02):
// Charge_Time_sec/Auto_Release are "always"; Min_Charge_Percent/Charge_Base/
// Min_Range/Pre_Charge_Delay are "if present" (this reader previously had all
// five non-bool scalars on the Always<T> family - this test pins the fix).
// ---------------------------------------------------------------------------
void testChargeReleaseInfoReaderFlavours() {
    // All five scalars authored: the "if present" fields must take the
    // authored value (not some fabricated/default value).
    {
        const char* xml =
            "<root><Table><Weapon><Name>w1</Name><Charge_Release_Info>"
            "<Charge_Time_sec>4.0</Charge_Time_sec><Min_Charge_Percent>0.25</Min_Charge_Percent>"
            "<Charge_Base>1.5</Charge_Base><Auto_Release>true</Auto_Release>"
            "<Min_Range>2.0</Min_Range><Pre_Charge_Delay>0.5</Pre_Charge_Delay>"
            "</Charge_Release_Info></Weapon></Table></root>";
        Document doc = P(xml);
        Weapon w = ParseWeapon(FindChild(doc.table(), "Weapon"));
        CHECK(w.chargeReleaseInfo.chargeTimeReciprocal.present && near(w.chargeReleaseInfo.chargeTimeReciprocal.value, 0.25f, 1e-6f));
        CHECK(w.chargeReleaseInfo.minChargePercent.has_value() && *w.chargeReleaseInfo.minChargePercent == 0.25f);
        CHECK(w.chargeReleaseInfo.chargeBase.has_value() && *w.chargeReleaseInfo.chargeBase == 1.5f);
        CHECK(w.chargeReleaseInfo.autoRelease.present && w.chargeReleaseInfo.autoRelease.value == true);
        CHECK(w.chargeReleaseInfo.minRange.has_value() && *w.chargeReleaseInfo.minRange == 2.0f);
        CHECK(w.chargeReleaseInfo.preChargeDelay.has_value() && *w.chargeReleaseInfo.preChargeDelay == 0.5f);
    }
    // Wrapper entirely absent: chargeTimeReciprocal ("always") must read back
    // present=true/value=0 (the real engine's unconditional +0x460=0 preset -
    // NOT the same as "untouched"); the four "if present" scalars and
    // Auto_Release must read back absent, not a fabricated default - this
    // reader does not attempt to model the documented 1.0/-1.0 defaults when
    // the whole wrapper (as opposed to just one field inside it) is missing,
    // per the caveat in weapons.h's ChargeReleaseInfo comment.
    {
        Document doc = P("<root><Table><Weapon><Name>w2</Name></Weapon></Table></root>");
        Weapon w = ParseWeapon(FindChild(doc.table(), "Weapon"));
        CHECK(!w.chargeReleaseInfo.chargeTimeReciprocal.present);  // Charge_Time_sec child itself absent -> this reader's Always<T> convention: present=false
        CHECK(!w.chargeReleaseInfo.minChargePercent.has_value());
        CHECK(!w.chargeReleaseInfo.chargeBase.has_value());
        CHECK(!w.chargeReleaseInfo.autoRelease.present);
        CHECK(!w.chargeReleaseInfo.minRange.has_value());
        CHECK(!w.chargeReleaseInfo.preChargeDelay.has_value());
    }
}

// ---------------------------------------------------------------------------
// weapon_categories.xtbl (spec section 3): a small flag-list/enum-index table.
// ---------------------------------------------------------------------------
void testWeaponCategoriesEnum() {
    const char* xml =
        "<root><Table>"
        "<Categories><Name>WPNCAT_MELEE</Name></Categories>"
        "<Categories><Name>WPNCAT_SPECIAL</Name></Categories>"
        "<Categories><Name>Not_A_Real_Category</Name></Categories>"
        "</Table></root>";
    Document doc = P(xml);
    auto rows = ParseAllWeaponCategories(doc);
    CHECK(rows.size() == 3);
    CHECK(rows[0].categoryIndex == 0);
    CHECK(rows[1].categoryIndex == 6);
    CHECK(rows[2].categoryIndex == -1);  // unmatched name -> -1, per sr3xtbl::EnumIndex (spec section 3: "the loader trusts the data")
}

// ---------------------------------------------------------------------------
// ammo.xtbl (spec section 5): a 12-literal flag list, plus boundary values.
// ---------------------------------------------------------------------------
void testAmmoFlagsAndBoundaries() {
    const char* xml =
        "<root><Table>"
        "<Ammo><Name>ammo_a</Name><Flags><Flag>bullet</Flag><Flag>armor piercing</Flag></Flags>"
        "<Max_in_Reserve>200</Max_in_Reserve><Inv_Slot>pistol</Inv_Slot>"
        "<Upgradable_Ammo><Weapon>sp_pistol01_w</Weapon><Level_2_Ammo>10</Level_2_Ammo></Upgradable_Ammo>"
        "<store><cost>50</cost><clip_size>12</clip_size></store>"
        "</Ammo>"
        "<Ammo><Name>ammo_b</Name><Inv_Slot>NoSuchSlot</Inv_Slot></Ammo>"  // no store, no Upgradable_Ammo, unmatched Inv_Slot
        "</Table></root>";
    Document doc = P(xml);
    auto rows = ParseAllAmmo(doc);
    CHECK(rows.size() == 2);
    CHECK((rows[0].flags & 0x8u) != 0);       // "bullet"
    CHECK((rows[0].flags & 0x800u) != 0);     // "armor piercing"
    CHECK(rows[0].invSlot == 2);              // "pistol" is index 2 of the 11-name table
    CHECK(rows[0].upgradableAmmoWeapon.has_value());
    CHECK(rows[0].upgradableAmmoLevel2.present && rows[0].upgradableAmmoLevel2.value == 10);
    CHECK(rows[0].storeCost.present && rows[0].storeCost.value == 50);
    CHECK(rows[0].storeClipSize.present && rows[0].storeClipSize.value == 12);

    CHECK(rows[1].flags == 0);
    CHECK(rows[1].invSlot == 0xFF);           // unmatched -> 0xFF
    CHECK(!rows[1].storeCost.present);        // "left as allocated (not written) if store is absent" - present must be false, not a fabricated 0
    CHECK(!rows[1].storeClipSize.present);
    CHECK(!rows[1].upgradableAmmoWeapon.has_value());
}

// ---------------------------------------------------------------------------
// melee.xtbl AttackLimb (spec section 7.1): 0 if absent, -1 if present and
// unmatched, else the matched index - three distinct values, easy to conflate.
// ---------------------------------------------------------------------------
void testMeleeAttackLimbBoundary() {
    Document noImpact = P("<root><Table><MeleeMove><Name>m1</Name></MeleeMove></Table></root>");
    MeleeMove m1 = ParseMeleeMove(FindChild(noImpact.table(), "MeleeMove"));
    CHECK(m1.attackLimb == 0);       // absent Impact -> 0
    CHECK(m1.attackLimbNpc == 0);    // NPC defaults to the player value

    Document unmatched = P("<root><Table><MeleeMove><Name>m2</Name><Impact><AttackLimb>Not A Limb</AttackLimb></Impact></MeleeMove></Table></root>");
    MeleeMove m2 = ParseMeleeMove(FindChild(unmatched.table(), "MeleeMove"));
    CHECK(m2.attackLimb == -1);      // present but unmatched -> -1

    Document matched = P(
        "<root><Table><MeleeMove><Name>m3</Name><Impact><AttackLimb>Left foot</AttackLimb>"
        "<AttackLimbNPC>Weapon</AttackLimbNPC></Impact></MeleeMove></Table></root>");
    MeleeMove m3 = ParseMeleeMove(FindChild(matched.table(), "MeleeMove"));
    CHECK(m3.attackLimb == 3);       // "Left foot" is index 3
    CHECK(m3.attackLimbNpc == 5);    // "Weapon" is index 5, explicitly authored (does not default to attackLimb)

    // The combined +0xF8 flag word: Testicular_Assault and a non-empty
    // Active_Attack_Infos each contribute a bit no <Flag> literal can set.
    Document withExtras = P(
        "<root><Table><MeleeMove><Name>m4</Name><Testicular_Assault>true</Testicular_Assault>"
        "<Active_Attack_Infos><Entry><Success_Extra_Damage>1</Success_Extra_Damage></Entry></Active_Attack_Infos>"
        "<Attack_is_for><Flag>Hard hit (LT)</Flag></Attack_is_for>"
        "</MeleeMove></Table></root>");
    MeleeMove m4 = ParseMeleeMove(FindChild(withExtras.table(), "MeleeMove"));
    CHECK(m4.testicularAssault);
    CHECK((m4.flagsF8 & 0x800000u) != 0);   // Testicular_Assault
    CHECK((m4.flagsF8 & 0x2000000u) != 0);  // non-empty Active_Attack_Infos
    CHECK((m4.flagsF8 & 0x2u) != 0);        // "Hard hit (LT)" bit 1
    CHECK(m4.activeAttackInfos.size() == 1);
}

// ---------------------------------------------------------------------------
// explosions.xtbl (spec section 10.1): a stored-squared field, and the
// case-sensitivity-adjacent "only 'yes', not 'true'" boundary.
// ---------------------------------------------------------------------------
void testExplosionsBoundary() {
    const char* xml =
        "<root><Table>"
        "<Explosion><Name>expl_a</Name><Radius>10</Radius><Redundant_Effect_Distance>4</Redundant_Effect_Distance>"
        "<Sticky_Fire><Always_Produce>yes</Always_Produce></Sticky_Fire></Explosion>"
        "<Explosion><Name>expl_b</Name><Radius>5</Radius>"
        "<Sticky_Fire><Always_Produce>true</Always_Produce></Sticky_Fire></Explosion>"
        "<Explosion><Name>expl_c</Name><Radius>5</Radius></Explosion>"
        "</Table></root>";
    Document doc = P(xml);
    auto rows = ParseAllExplosions(doc);
    CHECK(rows.size() == 3);
    CHECK(rows[0].redundantEffectDistanceSquared.has_value() && *rows[0].redundantEffectDistanceSquared == 16.0f);  // 4*4
    CHECK(rows[0].stickyFireAlwaysProduce == true);    // "yes"
    CHECK(rows[1].stickyFireAlwaysProduce == false);   // "true" is NOT recognised here (spec section 10.1: only "yes")
    CHECK(!rows[2].redundantEffectDistanceSquared.has_value());  // absent -> nullopt, not a fabricated 0
    CHECK(!rows[2].coneAngleRadians.has_value());                // documented engine default -1.0 is NOT invented here
}

// ---------------------------------------------------------------------------
// explosions.xtbl Tone/Tint (spec section 10.1, CORRECTED 2026-10-02): the
// engine stores R/G/B normalised to [0.0, 1.0] (divided by 255.0), NOT the
// raw 0-255 integers the XML authors - this reader previously stored the raw
// ints. Also pins Panic_Reaction/Groundfire as unmodified raw text (their
// resolver mechanisms differ per the correction, but neither changes what
// this reader stores).
// ---------------------------------------------------------------------------
void testExplosionTintNormalisation() {
    const char* xml =
        "<root><Table>"
        "<Explosion><Name>expl_tint</Name><Radius>1</Radius><Panic_Reaction>flee</Panic_Reaction>"
        "<Groundfire>burning_ground</Groundfire>"
        "<Screen_Effects><Tone><Tint><R>255</R><G>128</G><B>0</B></Tint></Tone></Screen_Effects>"
        "</Explosion>"
        "<Explosion><Name>expl_no_tint</Name><Radius>1</Radius>"
        "<Screen_Effects><Tone><Tint_scale>1</Tint_scale></Tone></Screen_Effects></Explosion>"
        "</Table></root>";
    Document doc = P(xml);
    auto rows = ParseAllExplosions(doc);
    CHECK(rows.size() == 2);
    CHECK(rows[0].panicReaction.has_value() && *rows[0].panicReaction == "flee");   // raw text, unresolved (resolver out of scope)
    CHECK(rows[0].groundfire.has_value() && *rows[0].groundfire == "burning_ground");  // raw text, unresolved (resolver out of scope)
    CHECK(rows[0].screenEffectsTintR.present && near(rows[0].screenEffectsTintR.value, 1.0f, 1e-6f));     // 255/255.0
    CHECK(rows[0].screenEffectsTintG.present && near(rows[0].screenEffectsTintG.value, 128.0f / 255.0f, 1e-6f));
    CHECK(rows[0].screenEffectsTintB.present && near(rows[0].screenEffectsTintB.value, 0.0f, 1e-6f));     // 0/255.0 == 0, but present must still be true
    CHECK(!rows[1].screenEffectsTintR.present);  // no <Tint> at all under this row's Tone - "always" reader: present=false, not a fabricated 0
}

// ---------------------------------------------------------------------------
// continuous_explosions.xtbl (spec section 10.2): Target_Info's positional
// 4-tuples, matched by content rather than by any fixed tag name.
// ---------------------------------------------------------------------------
void testContinuousExplosionsPositionalGrouping() {
    const char* xml =
        "<root><Table><Continuous_explosion><Name>barrage_a</Name>"
        "<Target_Info>"
        "<Target_Type>in_car</Target_Type><Radius_Min>1</Radius_Min><Radius_Max>2</Radius_Max><Target_FOV>90</Target_FOV>"
        "<Target_Type>default</Target_Type><Radius_Min>3</Radius_Min><Radius_Max>4</Radius_Max><Target_FOV>180</Target_FOV>"
        "</Target_Info>"
        "</Continuous_explosion></Table></root>";
    Document doc = P(xml);
    ContinuousExplosion ce = ParseContinuousExplosion(FindChild(doc.table(), "Continuous_explosion"));
    CHECK(ce.targetInCar.present && ce.targetInCar.radiusMin.value == 1.0f && ce.targetInCar.radiusMax.value == 2.0f);
    CHECK(near(ce.targetInCar.targetFovHalfAngleRadians.value, (90.0f * kDegToRad) * 0.5f, 1e-5f));
    CHECK(ce.targetDefault.present && ce.targetDefault.radiusMin.value == 3.0f);
    CHECK(!ce.targetInAircraft.present);  // never authored in this row
}

// ---------------------------------------------------------------------------
// combat_tricks.xtbl (spec section 11.2): the 18-slot indexed array and the
// Multi_Kill-only Kill_Duration field.
// ---------------------------------------------------------------------------
void testCombatTricksIndexedArray() {
    const char* xml =
        "<root><Table><Combat_Tricks>"
        "<Default_Duration>5</Default_Duration>"
        "<Gang_Kill><Max_Respect>10</Max_Respect></Gang_Kill>"
        "<Multi_Kill><Max_Respect>20</Max_Respect><Kill_Duration>1.5</Kill_Duration></Multi_Kill>"
        "</Combat_Tricks></Table></root>";
    Document doc = P(xml);
    CombatTricksRow r = ParseCombatTricks(doc);
    CHECK(r.defaultDurationMs.present && r.defaultDurationMs.value == 5.0f);
    CHECK(r.tricks[0].present && r.tricks[0].maxRespect.value == 10);   // Gang_Kill == index 0
    CHECK(!r.tricks[1].present);                                        // Gang_Vehicle_Kill not authored
    CHECK(r.tricks[7].present && r.tricks[7].maxRespect.value == 20);   // Multi_Kill == index 7
    CHECK(r.tricks[7].killDurationMs.has_value() && *r.tricks[7].killDurationMs == 1500);  // seconds -> ms
    CHECK(!r.tricks[0].killDurationMs.has_value());  // Kill_Duration is Multi_Kill-only
}

// ---------------------------------------------------------------------------
// combat_tricks.xtbl static-table row count and the `Beat_Down_Kill` question
// (spec section 11.2, CONFIRMED/CORRECTED 2026-10-02: the static table is
// exactly 18 rows; its 10th entry is `Brute_Beat_Kill`, and there is no
// `..._BEAT_DOWN_KILL` key anywhere in it - a real row's `Beat_Down_Kill`
// child is simply never looked up by this loader, distinct from and
// additional to whatever `Brute_Beat_Kill` itself contains). This reader was
// already correct on this point before the spec sync (kCombatTrickNames has
// always had exactly 18 entries with `Brute_Beat_Kill` at index 9, never
// `Beat_Down_Kill`); this test pins that down as a named regression so a
// future edit cannot silently reintroduce Team B's old "Beat_Down_Kill"
// shape or drop a row.
// ---------------------------------------------------------------------------
void testCombatTricksBeatDownKillIsUnread() {
    const char* xml =
        "<root><Table><Combat_Tricks>"
        "<Beat_Down_Kill><Max_Respect>99</Max_Respect></Beat_Down_Kill>"
        "<Brute_Beat_Kill><Max_Respect>42</Max_Respect></Brute_Beat_Kill>"
        "</Combat_Tricks></Table></root>";
    Document doc = P(xml);
    CombatTricksRow r = ParseCombatTricks(doc);
    CHECK(r.tricks[9].present && r.tricks[9].maxRespect.value == 42);  // Brute_Beat_Kill == index 9, the real 18-row table's 10th entry
    // No slot anywhere in the fixed 18 should have picked up the
    // Beat_Down_Kill child's value (99) - confirming it is genuinely unread,
    // not aliased onto some other trick by a name-matching accident.
    bool any99 = false;
    for (const auto& d : r.tricks) {
        if (d.present && d.maxRespect.present && d.maxRespect.value == 99) any99 = true;
    }
    CHECK(!any99);
}

// ---------------------------------------------------------------------------
// weapon_upgrades.xtbl (spec section 4): the operation-mode enum and the
// "text after the last ':' of _Editor/Category" weapon-name extraction.
// ---------------------------------------------------------------------------
void testWeaponUpgradeOmAndEditorCategory() {
    const char* xml =
        "<root><Table><Weapon_Upgrade><Name>up_a</Name>"
        "<_Editor><Category>Entries:Rifle:sp_rifle01_w</Category></_Editor>"
        "<Range_Max>600</Range_Max><Range_Max_OM>OM_ADDITIVE</Range_Max_OM>"
        "<Ragdoll_Force_Shoot>2</Ragdoll_Force_Shoot><Ragdoll_Force_Shoot_OM>OM_MULTIPLICATIVE</Ragdoll_Force_Shoot_OM>"
        "<Magazine_Size>40</Magazine_Size>"  // no _OM sibling -> Replace (spec section 4.2 default)
        "<Damage_Max_Dist>80</Damage_Max_Dist><Damage_Max_Dist_OM>OM_REMOVAL</Damage_Max_Dist_OM>"
        "</Weapon_Upgrade></Table></root>";
    Document doc = P(xml);
    WeaponUpgrade u = ParseWeaponUpgrade(FindChild(doc.table(), "Weapon_Upgrade"));
    CHECK(u.targetWeaponName.has_value() && *u.targetWeaponName == "sp_rifle01_w");
    CHECK(u.rangeMax.has_value() && u.rangeMax->value == 600.0f && u.rangeMax->mode == OperationMode::Additive);
    CHECK(u.ragdollForceShoot.has_value() && u.ragdollForceShoot->mode == OperationMode::Multiplicative);
    CHECK(u.magazineSize.has_value() && u.magazineSize->value == 40 && u.magazineSize->mode == OperationMode::Replace);
    // OM_REMOVAL (spec section 4.2, CONFIRMED/CORRECTED 2026-10-02): this
    // reader only parses/exposes the mode value - it does not apply patches
    // (that dispatch is out of scope here, see combat.h's OperationMode
    // comment) - so the only thing to pin down at this layer is that the
    // encoding still round-trips Removal distinctly from Replace, even
    // though the real dispatcher treats both the same way downstream.
    CHECK(u.damageMaxDist.has_value() && u.damageMaxDist->value == 80.0f && u.damageMaxDist->mode == OperationMode::Removal);
    CHECK(!u.fireConeLength.has_value());  // not authored at all
}

// ---------------------------------------------------------------------------
// hostage.xtbl (spec section 14.3): nested enum + repeated difficulty levels.
// ---------------------------------------------------------------------------
void testHostageNestedEnumAndLevels() {
    const char* xml =
        "<root><Table><Hostage><Min_Notoriety>2</Min_Notoriety>"
        "<Vehicle_Classes><Vehicle_Class><Class_Name>SUV</Class_Name>"
        "<Difficulty_Levels>"
        "<Difficulty_Level><Num_Hostages>1</Num_Hostages><Respect>10</Respect></Difficulty_Level>"
        "<Difficulty_Level><Num_Hostages>3</Num_Hostages><Respect>30</Respect></Difficulty_Level>"
        "</Difficulty_Levels></Vehicle_Class></Vehicle_Classes>"
        "</Hostage></Table></root>";
    Document doc = P(xml);
    HostageRow r = ParseHostage(doc);
    CHECK(r.minNotoriety.has_value() && *r.minNotoriety == 2);
    CHECK(r.vehicleClasses.size() == 1);
    CHECK(r.vehicleClasses[0].classIndex == 4);  // "SUV" is index 4 of the 6-name table
    CHECK(r.vehicleClasses[0].difficultyLevels.size() == 2);
    CHECK(r.vehicleClasses[0].difficultyLevels[0].numHostages.value() == 1);
    CHECK(r.vehicleClasses[0].difficultyLevels[1].respect.value() == 30);
}

// spec section 8 (CONFIRMED 2026-10-02 - previously OPEN/NEEDS-EXE): a profile
// with no Bullet_miss/Recovery child must be REPORTED (diagnostics), and one
// with the child must not be. The `Bullet_miss -> Recovery` path and the
// "defaults preset unconditionally before the presence check" behaviour are
// both now confirmed correct, not a mis-association - see combat.h.
void testAimDriftRecoveryReport() {
    const char* withoutRecovery =
        "<root><Table><Profile><Name>Default</Name><turn_speed>1</turn_speed>"
        "<Bullet_miss><Aiming><Close_range>1</Close_range></Aiming><Firing/></Bullet_miss>"
        "<Explosive_Miss/></Profile></Table></root>";
    Document d1 = P(withoutRecovery);
    auto v1 = ParseAllAimDriftProfiles(d1);
    CHECK(v1.size() == 1);
    CHECK(v1[0].diagnostics.size() == 1);
    CHECK(!v1[0].diagnostics.empty() && v1[0].diagnostics[0] == kAimDriftRecoveryEmptyDiagnostic);
    CHECK(!v1[0].recoveryTime.present);

    const char* withRecovery =
        "<root><Table><Profile><Name>X</Name>"
        "<Bullet_miss><Aiming/><Recovery><recover_time>700</recover_time></Recovery><Firing/></Bullet_miss>"
        "<Explosive_Miss/></Profile></Table></root>";
    Document d2 = P(withRecovery);
    auto v2 = ParseAllAimDriftProfiles(d2);
    CHECK(v2.size() == 1);
    CHECK(v2[0].diagnostics.empty());
    CHECK(v2[0].recoveryTime.present && near(v2[0].recoveryTime.value, 700.0f, 1e-4f));
}

}  // namespace

int main() {
    testAimDriftRecoveryReport();
    testWeaponNormalRow();
    testWeaponMissingOptional();
    testWeaponFlagsAndSpecialCases();
    testChargeReleaseInfoReaderFlavours();
    testWeaponCategoriesEnum();
    testAmmoFlagsAndBoundaries();
    testMeleeAttackLimbBoundary();
    testExplosionsBoundary();
    testExplosionTintNormalisation();
    testContinuousExplosionsPositionalGrouping();
    testCombatTricksIndexedArray();
    testCombatTricksBeatDownKillIsUnread();
    testWeaponUpgradeOmAndEditorCategory();
    testHostageNestedEnumAndLevels();

    if (g_failed) {
        std::cerr << g_failed << " check(s) FAILED (" << g_passed << " passed).\n";
        return 1;
    }
    std::cout << g_passed << " tests passed.\n";
    return 0;
}
