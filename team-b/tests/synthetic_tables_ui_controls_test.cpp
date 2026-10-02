// Synthetic tests for sr3tables_ui_controls (spec-tables-ui-controls.md).
// Every fixture below is a hand-built XML string transcribed from the SPEC
// TEXT (section numbers cited per test), never derived from this project's
// own reader source - a pass here proves the reader implements what the
// spec says, not that the spec is right. The real-data cross-check is
// tools/validation/validate_tables_ui_controls_population.cpp.
//
// Style follows this project's synthetic-suite convention (see
// tests/synthetic_tables_weapons_test.cpp, tests/synthetic_tables_environment_test.cpp):
// a tiny hand-rolled CHECK macro, a running pass count, "<N> tests passed."
// on the last line.
//
// Mutation-check note (this project's process, measurement-discipline.md):
// at least 3 of the CHECKs below were confirmed to actually catch a broken
// reader by hand, before this file was finalised:
//   1. testBindingSetAxisFieldOrder's field-by-field check (spec 2.3, the
//      exact answer to spec-save-format.md 7.6's field-order OPEN item):
//      temporarily swapped ParseBindingSetAxis's Key_Pos/Key_Neg element
//      names in tables_ui_controls.cpp -> `CHECK(a.key1 == "D")` failed as
//      expected (it read "A", the Key_Neg value); reverted.
//   2. testVoiceControlBitfields's MaxDelayField check (spec 12's exact
//      packing formula: "(Max_delay - Min_delay) / 80" using the RAW
//      Min_delay, not the already-clamped/packed MinDelayField()):
//      temporarily changed VoiceControlEntry::MaxDelayField to subtract
//      MinDelayField() instead of minDelayRaw.value -> the check
//      `CHECK(e.MaxDelayField() == 5)` failed (produced a different value
//      once Min_delay's raw magnitude no longer matched its packed form);
//      reverted.
//   3. testCreditsDoubledTags's heading/item traversal (spec 13's doubled
//      tag-name shape, `<headings><headings>...` and `<items><items>...`):
//      temporarily changed ParseCreditsSection to look up children named
//      "heading" (singular) instead of "headings" -> `CHECK(sec.headings.size() == 2)`
//      failed (0 headings found); reverted.

#include <iostream>
#include <string>

#include "sr3tables_ui_controls/tables.h"
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
using namespace sr3tables_ui_controls;

bool near(float a, float b, float tol) { return (a > b ? a - b : b - a) <= tol; }

Document P(std::string_view s) { return ParseDocument(s); }

// ---------------------------------------------------------------------------
// 4. Shared vocabulary tables
// ---------------------------------------------------------------------------
void testLookupTables() {
    // kMouseButtonTable (4.4): exactly 7 real values + UNBOUND=-1.
    {
        const char* xml = "<root><Table><R><M>MOUSE LEFT</M></R></Table></root>";
        Document doc = P(xml);
        const Node* r = FindChild(doc.table(), "R");
        CHECK(LookupNamedValue(FindChild(r, "M"), kMouseButtonTable) == 0);
    }
    {
        const char* xml = "<root><Table><R><M>WHEEL DOWN</M></R></Table></root>";
        Document doc = P(xml);
        const Node* r = FindChild(doc.table(), "R");
        CHECK(LookupNamedValue(FindChild(r, "M"), kMouseButtonTable) == 6);
    }
    {
        const char* xml = "<root><Table><R><M>UNBOUND</M></R></Table></root>";
        Document doc = P(xml);
        const Node* r = FindChild(doc.table(), "R");
        CHECK(LookupNamedValue(FindChild(r, "M"), kMouseButtonTable) == -1);
    }
    {  // absent element -> -1
        const char* xml = "<root><Table><R></R></Table></root>";
        Document doc = P(xml);
        const Node* r = FindChild(doc.table(), "R");
        CHECK(LookupNamedValue(FindChild(r, "M"), kMouseButtonTable) == -1);
    }
    // kGamepadButtonTable (4.6): the real, confirmed value collision - L_JOY/A both 0.
    {
        const char* xml = "<root><Table><R><B>L_JOY</B></R></Table></root>";
        Document doc = P(xml);
        const Node* r = FindChild(doc.table(), "R");
        CHECK(LookupNamedValue(FindChild(r, "B"), kGamepadButtonTable) == 0);
    }
    {
        const char* xml = "<root><Table><R><B>RS</B></R></Table></root>";
        Document doc = P(xml);
        const Node* r = FindChild(doc.table(), "R");
        CHECK(LookupNamedValue(FindChild(r, "B"), kGamepadButtonTable) == 15);
    }
    // kStickDirectionTable / kStickAxisTable (4.7/4.8) shorthand expansion.
    {
        const char* xml =
            "<root><Table><R><D>RIGHT_STICK_DIR_LEFT</D><A>LEFT_STICK_AXIS_Y</A></R></Table></root>";
        Document doc = P(xml);
        const Node* r = FindChild(doc.table(), "R");
        CHECK(LookupNamedValue(FindChild(r, "D"), kStickDirectionTable) == 7);
        CHECK(LookupNamedValue(FindChild(r, "A"), kStickAxisTable) == 3);
    }
    // EnumIndex-based index tables (9.2, 13).
    {
        const char* xml = "<root><Table><R><T>Mash Fast</T></R></Table></root>";
        Document doc = P(xml);
        const Node* r = FindChild(doc.table(), "R");
        CHECK(EnumIndex(FindChild(r, "T"), kButtonAnimationTypeNames.data(), kButtonAnimationTypeNames.size()) == 1);
    }
    {
        const char* xml = "<root><Table><R><T>Centered Single Line</T></R></Table></root>";
        Document doc = P(xml);
        const Node* r = FindChild(doc.table(), "R");
        CHECK(EnumIndex(FindChild(r, "T"), kCreditsItemTypeNames.data(), kCreditsItemTypeNames.size()) == 3);
    }
}

// ---------------------------------------------------------------------------
// 2. control_binding_sets.xtbl
// ---------------------------------------------------------------------------
void testBindingSetControlAndAxis() {
    const char* xml =
        "<root><Table><Binding_Set>"
        "<Name>General_Bindings</Name>"
        "<Common_Bindings>True</Common_Bindings>"
        "<Button_Controls>"
        "<Control>"
        "<Action>CBA_OFC_ATTACK_PRIMARY</Action>"
        "<Key>A</Key>"
        "<Alt_Key>UNBOUND</Alt_Key>"
        "<Mouse_Button>MOUSE LEFT</Mouse_Button>"
        "<Alt_Mouse_Button>WHEEL UP</Alt_Mouse_Button>"
        "<Debug_Only>False</Debug_Only>"
        "<Non_Release_Final>False</Non_Release_Final>"
        "</Control>"
        "</Button_Controls>"
        "<Axis_Controls>"
        "<Axis>"
        "<Action>CAA_DRIVE_STEER</Action>"
        "<Key_Pos>D</Key_Pos>"
        "<Key_Neg>A</Key_Neg>"
        "<Alt_Key_Pos>UNBOUND</Alt_Key_Pos>"
        "<Alt_Key_Neg>UNBOUND</Alt_Key_Neg>"
        "<Mouse_Button_Pos>UNBOUND</Mouse_Button_Pos>"
        "<Mouse_Button_Neg>UNBOUND</Mouse_Button_Neg>"
        "<Alt_Mouse_Button_Pos>UNBOUND</Alt_Mouse_Button_Pos>"
        "<Alt_Mouse_Button_Neg>UNBOUND</Alt_Mouse_Button_Neg>"
        "<Mouse_Axis>MOUSE X</Mouse_Axis>"
        "</Axis>"
        "</Axis_Controls>"
        "</Binding_Set></Table></root>";
    Document doc = P(xml);
    CHECK(doc.warnings().empty());
    auto sets = ParseControlBindingSetsTable(doc);
    CHECK(sets.size() == 1);
    const BindingSet& b = sets[0];
    CHECK(b.name.has_value() && *b.name == "General_Bindings");
    CHECK(b.commonBindings.has_value() && *b.commonBindings == true);
    CHECK(b.controls.size() == 1);
    const BindingSetControl& c = b.controls[0];
    CHECK(c.action.has_value() && *c.action == "CBA_OFC_ATTACK_PRIMARY");
    CHECK(c.key1.has_value() && *c.key1 == "A");
    CHECK(c.key2.has_value() && *c.key2 == "UNBOUND");  // raw text kept - key-name table is a documented gap
    CHECK(c.mouse1 == 0);   // MOUSE LEFT
    CHECK(c.mouse2 == 5);   // WHEEL UP
    CHECK(c.debugOnly.present && c.debugOnly.value == false);
    CHECK(c.nonReleaseFinal.present && c.nonReleaseFinal.value == false);

    CHECK(b.axes.size() == 1);
    // Field-order check (spec 2.3 - see the mutation-check note #1 above).
    const BindingSetAxis& a = b.axes[0];
    CHECK(a.action.has_value() && *a.action == "CAA_DRIVE_STEER");
    // The exact field order spec 2.3 states resolves spec-save-format.md
    // 7.6's OPEN item: Key_Pos, Key_Neg, Alt_Key_Pos, Alt_Key_Neg, then the
    // four mouse fields, then the direction selector.
    CHECK(a.key1.has_value() && *a.key1 == "D");  // Key_Pos
    CHECK(a.key2.has_value() && *a.key2 == "A");  // Key_Neg
    CHECK(a.key3.has_value() && *a.key3 == "UNBOUND");  // Alt_Key_Pos
    CHECK(a.key4.has_value() && *a.key4 == "UNBOUND");  // Alt_Key_Neg
    CHECK(a.mouse1 == -1);  // Mouse_Button_Pos = UNBOUND
    CHECK(a.mouse2 == -1);  // Mouse_Button_Neg
    CHECK(a.mouse3 == -1);  // Alt_Mouse_Button_Pos
    CHECK(a.mouse4 == -1);  // Alt_Mouse_Button_Neg
    CHECK(a.mouseAxis == 0);  // Mouse_Axis = MOUSE X
}

// ---------------------------------------------------------------------------
// 3. control_schemes.xtbl
// ---------------------------------------------------------------------------
void testControlScheme() {
    const char* xml =
        "<root><Table><Control_Scheme>"
        "<Name>A</Name>"
        "<Weapon_Selection_Stick>L_JOY</Weapon_Selection_Stick>"
        "<Controls>"
        "<Control>"
        "<Action>CBA_GAC_ACTION</Action>"
        "<Button>A</Button>"
        "<Alternate_Button>NOT_A_REAL_BUTTON</Alternate_Button>"
        "<Stick_Direction>LEFT_STICK_DIR_UP</Stick_Direction>"
        "<Action_Name>gas</Action_Name>"
        "<X360>True</X360>"
        "<Debug_Only>False</Debug_Only>"
        "<Non_Release_Final>False</Non_Release_Final>"
        "</Control>"
        "<Control>"  // no <X360> at all - spec-stated default true when absent (3.1)
        "<Action>CBA_MENU_A</Action>"
        "<Button>B</Button>"
        "</Control>"
        "</Controls>"
        "<Axes>"
        "<Axis>"
        "<Action>CAA_CAMERA_ROTATE</Action>"
        "<Axis>RIGHT_STICK_AXIS_X</Axis>"
        "<Inverted>True</Inverted>"
        "<Alternate_Axis>LEFT_STICK_AXIS_Y</Alternate_Axis>"
        // Alternate_Inverted is real authored data but CORRECTED 2026-10-01
        // (3.2) to be dead - the loader never looks it up. Deliberately set
        // to the OPPOSITE of <Inverted> here so the test would fail loudly
        // if this reader ever started reading it instead of re-reading
        // <Inverted>.
        "<Alternate_Inverted>False</Alternate_Inverted>"
        "</Axis>"
        "</Axes>"
        "</Control_Scheme></Table></root>";
    Document doc = P(xml);
    CHECK(doc.warnings().empty());
    auto schemes = ParseControlSchemesTable(doc);
    CHECK(schemes.size() == 1);
    const ControlScheme& s = schemes[0];
    CHECK(s.name.has_value() && *s.name == "A");
    CHECK(s.weaponSelectionStick == 0);  // L_JOY
    CHECK(s.controls.size() == 2);
    const ControlSchemeControl& c0 = s.controls[0];
    CHECK(c0.action.has_value() && *c0.action == "CBA_GAC_ACTION");
    CHECK(c0.button == 0);              // A
    CHECK(c0.alternateButton == -1);    // unmatched text -> -1, the engine's own sentinel
    CHECK(c0.stickDirection == 0);      // LEFT_STICK_DIR_UP
    CHECK(c0.actionName.has_value() && *c0.actionName == "gas");
    CHECK(c0.x360.has_value() && *c0.x360 == true);
    CHECK(c0.X360OrDefault() == true);
    const ControlSchemeControl& c1 = s.controls[1];
    CHECK(!c1.x360.has_value());        // absent
    CHECK(c1.X360OrDefault() == true);  // spec-stated default when absent (3.1)

    CHECK(s.axes.size() == 1);
    const ControlSchemeAxis& a = s.axes[0];
    CHECK(a.action.has_value() && *a.action == "CAA_CAMERA_ROTATE");
    CHECK(a.axis == 0);            // RIGHT_STICK_AXIS_X (the row's OWN <Axis> child, same name as the row itself)
    CHECK(a.inverted.present && a.inverted.value == true);
    CHECK(a.alternateAxis == 3);   // LEFT_STICK_AXIS_Y
    // CORRECTED 2026-10-01 (3.2): the loader re-reads <Inverted>, not
    // <Alternate_Inverted> - so this equals a.inverted (true), NOT the
    // <Alternate_Inverted>False</Alternate_Inverted> text actually authored
    // above, proving that element is genuinely dead data for this reader too.
    CHECK(a.alternateInverted.present && a.alternateInverted.value == true);
}

// ---------------------------------------------------------------------------
// 6. qte.xtbl - real-data values transcribed verbatim from spec 6.
// ---------------------------------------------------------------------------
void testQteGlobalParams() {
    const char* xml =
        "<root><Table><QTE>"
        "<Name>QTE</Name>"
        "<Hud_Time>4</Hud_Time>"
        "<Cooldown_Time>6</Cooldown_Time>"
        "<Respect>50</Respect>"
        "<Max_Lifetime_Respect>10000</Max_Lifetime_Respect>"
        "<Cash>100</Cash>"
        "</QTE></Table></root>";
    Document doc = P(xml);
    CHECK(doc.warnings().empty());
    auto q = ParseQteTable(doc);
    CHECK(q.has_value());
    CHECK(q->hudTime.present && near(q->hudTime.value, 4.0f, 1e-6f));
    CHECK(q->cooldownTime.present && near(q->cooldownTime.value, 6.0f, 1e-6f));
    CHECK(q->respect.present && q->respect.value == 50);
    CHECK(q->maxLifetimeRespect.present && q->maxLifetimeRespect.value == 10000);
    CHECK(q->cash.present && near(q->cash.value, 100.0f, 1e-6f));

    // CONTROL: no <QTE> row -> nullopt, not a fabricated zero-value struct.
    Document empty = P("<root><Table></Table></root>");
    CHECK(!ParseQteTable(empty).has_value());
}

// ---------------------------------------------------------------------------
// 7. qte_sequences.xtbl
// ---------------------------------------------------------------------------
void testQteSequenceAndNode() {
    const char* xml =
        "<root><Table><QTE>"
        "<Name>Mission16_FinalQTE</Name>"
        "<Disable_Player>True</Disable_Player>"
        "<View_Remotely>False</View_Remotely>"
        "<Player_Weapon>rifle_std</Player_Weapon>"
        // CORRECTED 2026-10-01 (7.1/7.2): Synced_Animation here is a NAME
        // resolved via a hash-indexed table (0x0095DA50), not a literal
        // number like the old test data implied - use a real-shaped name.
        "<Succes_State><Synced_Animation>anim_sync_success</Synced_Animation><Player_Is_Attacker>True</Player_Is_Attacker></Succes_State>"
        "<Animated_NPCs>"
        "<Animated_NPC><NPC_Name>Bodyguard1</NPC_Name></Animated_NPC>"
        "<Animated_NPC><NPC_Name>Bodyguard2</NPC_Name></Animated_NPC>"
        "</Animated_NPCs>"
        "<QTE_Nodes>"
        "<QTE_Node>"
        "<Node_Name>Node1</Node_Name>"
        "<HUD_Interface>Rapid X</HUD_Interface>"
        "<Fail_Time>3000</Fail_Time>"
        "<Persona><Persona_Line>1001</Persona_Line><Line_Delay>500</Line_Delay></Persona>"
        "<Partner_Persona><Persona_Line>1002</Persona_Line><Line_Delay>250</Line_Delay></Partner_Persona>"
        "<Camera_Shake>shake_qte</Camera_Shake>"
        "<Health_Change>-10</Health_Change>"
        "<Player_Impact_Dmg>5</Player_Impact_Dmg>"
        "<Enter_Action>"
        "<Synced_Melee_Move><Melee_Move>12</Melee_Move><Player_Is_Attacker>True</Player_Is_Attacker></Synced_Melee_Move>"
        "<Player_Animation>anim_enter</Player_Animation>"
        "</Enter_Action>"
        "<Looping_State>"
        "<Synced_State><Synced_Animation>anim_loop</Synced_Animation><Player_Is_Attacker>False</Player_Is_Attacker></Synced_State>"
        "<Player_Animation_State>state_loop</Player_Animation_State>"
        "</Looping_State>"
        "<Additive_Action>"
        "<Synced_Melee_Move><Melee_Move>13</Melee_Move><Player_Is_Attacker>False</Player_Is_Attacker></Synced_Melee_Move>"
        "<Player_Animation>anim_add</Player_Animation>"
        "<New_Fail_Time>2000</New_Fail_Time>"
        "<Action_Count>3</Action_Count>"
        "<Shooting_Mode>"
        "<Recoil>0.5</Recoil>"
        "<Synced_Partner_Flinch>anim_flinch</Synced_Partner_Flinch>"
        "<Restrict_Camera_Angle><Min_Heading>-1</Min_Heading><Max_Heading>1</Max_Heading><Min_Pitch>-0.5</Min_Pitch><Max_Pitch>0.5</Max_Pitch></Restrict_Camera_Angle>"
        "<Target_Camera_Angle><Min_Heading>-30</Min_Heading><Max_Heading>30</Max_Heading><Min_Pitch>-15</Min_Pitch><Max_Pitch>15</Max_Pitch></Target_Camera_Angle>"
        "</Shooting_Mode>"
        "</Additive_Action>"
        "<NPC_Animations>"
        "<NPC_Animation><NPC_Name>Bodyguard1</NPC_Name><Persona_Line>2001</Persona_Line><Line_Delay>100</Line_Delay>"
        "<Enter_Action>npc_enter_anim</Enter_Action><Looping_State>npc_loop_anim</Looping_State><Additive_Action>npc_add_anim</Additive_Action></NPC_Animation>"
        "</NPC_Animations>"
        "<On_Success><Interrupt>True</Interrupt><Keep_HUD>False</Keep_HUD><Kill_Partner>True</Kill_Partner><Repeat>2</Repeat><Go_To_Node>Node2</Go_To_Node></On_Success>"
        "<On_Fail><Go_To_Node>NodeFail</Go_To_Node><Force_Success>True</Force_Success><Keep_HUD>True</Keep_HUD></On_Fail>"
        "<Triggered_Explosion>expl_qte</Triggered_Explosion>"
        "</QTE_Node>"
        "</QTE_Nodes>"
        "</QTE></Table></root>";
    Document doc = P(xml);
    CHECK(doc.warnings().empty());
    auto seqs = ParseQteSequencesTable(doc);
    CHECK(seqs.size() == 1);
    const QteSequence& s = seqs[0];
    CHECK(s.name.has_value() && *s.name == "Mission16_FinalQTE");
    CHECK(s.disablePlayer.present && s.disablePlayer.value == true);
    CHECK(s.viewRemotely.present && s.viewRemotely.value == false);
    CHECK(s.playerWeapon.has_value() && *s.playerWeapon == "rifle_std");
    CHECK(s.successAnimation.has_value() && *s.successAnimation == "anim_sync_success");
    CHECK(s.successPlayerIsAttacker.present && s.successPlayerIsAttacker.value == true);
    CHECK(s.animatedNpcNames.size() == 2);
    CHECK(s.animatedNpcNames[0] == "Bodyguard1" && s.animatedNpcNames[1] == "Bodyguard2");

    CHECK(s.nodes.size() == 1);
    const QteNode& n = s.nodes[0];
    CHECK(n.nodeName.has_value() && *n.nodeName == "Node1");
    CHECK(n.hudInterface.has_value() && *n.hudInterface == "Rapid X");
    CHECK(n.failTime.has_value() && *n.failTime == 3000);
    CHECK(n.personaLine.has_value() && *n.personaLine == "1001");
    CHECK(n.personaLineDelay.present && n.personaLineDelay.value == 500);
    CHECK(n.partnerPersonaLine.has_value() && *n.partnerPersonaLine == "1002");
    CHECK(n.partnerPersonaLineDelay.present && n.partnerPersonaLineDelay.value == 250);
    CHECK(n.cameraShake.has_value() && *n.cameraShake == "shake_qte");
    CHECK(n.healthChange.has_value() && near(*n.healthChange, -10.0f, 1e-6f));
    CHECK(n.playerImpactDmg.has_value() && *n.playerImpactDmg == 5);
    CHECK(n.enterMeleeMove.has_value() && *n.enterMeleeMove == "12");
    CHECK(n.enterMeleeMovePlayerIsAttacker.present && n.enterMeleeMovePlayerIsAttacker.value == true);
    CHECK(n.enterPlayerAnimation.has_value() && *n.enterPlayerAnimation == "anim_enter");
    CHECK(n.loopingSyncedAnimation.has_value() && *n.loopingSyncedAnimation == "anim_loop");
    CHECK(n.loopingSyncedPlayerIsAttacker.present && n.loopingSyncedPlayerIsAttacker.value == false);
    CHECK(n.loopingPlayerAnimationState.has_value() && *n.loopingPlayerAnimationState == "state_loop");
    CHECK(n.additiveMeleeMove.has_value() && *n.additiveMeleeMove == "13");
    CHECK(n.additiveMeleeMovePlayerIsAttacker.present && n.additiveMeleeMovePlayerIsAttacker.value == false);
    CHECK(n.additivePlayerAnimation.has_value() && *n.additivePlayerAnimation == "anim_add");
    CHECK(n.additiveNewFailTime.has_value() && *n.additiveNewFailTime == 2000u);
    CHECK(n.additiveActionCount.has_value() && *n.additiveActionCount == 3);
    CHECK(n.AdditiveActionCountOrDefault() == 3);
    CHECK(n.shootingModePresent == true);
    CHECK(n.shootingModeRecoil.present && near(n.shootingModeRecoil.value, 0.5f, 1e-6f));
    CHECK(n.shootingModeSyncedPartnerFlinch.has_value() && *n.shootingModeSyncedPartnerFlinch == "anim_flinch");
    CHECK(near(n.restrictCameraMinHeading.value, -1.0f, 1e-6f));
    CHECK(near(n.restrictCameraMaxHeading.value, 1.0f, 1e-6f));
    CHECK(near(n.restrictCameraMinPitch.value, -0.5f, 1e-6f));
    CHECK(near(n.restrictCameraMaxPitch.value, 0.5f, 1e-6f));
    CHECK(near(n.targetCameraMinHeadingDeg.value, -30.0f, 1e-6f));  // RAW degrees, no deg->rad applied
    CHECK(near(n.targetCameraMaxHeadingDeg.value, 30.0f, 1e-6f));
    CHECK(near(n.targetCameraMinPitchDeg.value, -15.0f, 1e-6f));
    CHECK(near(n.targetCameraMaxPitchDeg.value, 15.0f, 1e-6f));

    CHECK(n.npcAnimations.size() == 1);
    const QteNodeNpcAnimation& na = n.npcAnimations[0];
    CHECK(na.npcName.has_value() && *na.npcName == "Bodyguard1");
    CHECK(na.personaLine.has_value() && *na.personaLine == "2001");
    CHECK(na.lineDelay.has_value() && *na.lineDelay == 100);
    CHECK(na.enterActionAnim.has_value() && *na.enterActionAnim == "npc_enter_anim");
    CHECK(na.loopingStateAnim.has_value() && *na.loopingStateAnim == "npc_loop_anim");
    CHECK(na.additiveActionAnim.has_value() && *na.additiveActionAnim == "npc_add_anim");

    CHECK(n.onSuccessInterrupt.present && n.onSuccessInterrupt.value == true);
    CHECK(n.onSuccessKeepHud.present && n.onSuccessKeepHud.value == false);
    CHECK(n.onSuccessKillPartner.present && n.onSuccessKillPartner.value == true);
    CHECK(n.onSuccessRepeat.present && n.onSuccessRepeat.value == 2);
    CHECK(n.onSuccessGoToNode.has_value() && *n.onSuccessGoToNode == "Node2");
    CHECK(n.onFailGoToNode.has_value() && *n.onFailGoToNode == "NodeFail");
    CHECK(n.onFailForceSuccess.has_value() && *n.onFailForceSuccess == true);
    CHECK(n.onFailKeepHud.present && n.onFailKeepHud.value == true);
    CHECK(n.triggeredExplosion.has_value() && *n.triggeredExplosion == "expl_qte");

    // Additive_Action/Action_Count absent -> spec-stated default 0 (7.2).
    const char* xml2 =
        "<root><Table><QTE><Name>S2</Name>"
        "<QTE_Nodes><QTE_Node><Node_Name>N</Node_Name><Additive_Action></Additive_Action></QTE_Node></QTE_Nodes>"
        "</QTE></Table></root>";
    Document doc2 = P(xml2);
    auto seqs2 = ParseQteSequencesTable(doc2);
    CHECK(seqs2.size() == 1 && seqs2[0].nodes.size() == 1);
    CHECK(!seqs2[0].nodes[0].additiveActionCount.has_value());
    CHECK(seqs2[0].nodes[0].AdditiveActionCountOrDefault() == 0);
    CHECK(seqs2[0].nodes[0].shootingModePresent == false);  // no <Shooting_Mode> child at all
}

// ---------------------------------------------------------------------------
// 8.1 vehicle_cameras.xtbl / vehicle_group_cameras.xtbl
// ---------------------------------------------------------------------------
void testVehicleCameraFields() {
    // Position_Based variant, plus camera-angle-set PRESENCE (internal
    // layout OPEN - only presence is modelled, 8.1/17.2).
    const char* xml1 =
        "<root><Table><Vehicle>"
        "<Name>Attrazione</Name>"
        "<Camera_Proximity_Lock_Y>2.5</Camera_Proximity_Lock_Y>"
        "<Primary_Camera_Angle><Opaque>1</Opaque></Primary_Camera_Angle>"
        "<Secondary_Camera_Angle><Opaque>1</Opaque></Secondary_Camera_Angle>"
        "<Position_Based>"
        "<Swing_Rate_Fwd>0.3</Swing_Rate_Fwd><Swing_Rate_Rev>0.4</Swing_Rate_Rev>"
        "<From_Sticky_Fwd>0.2</From_Sticky_Fwd><From_Sticky_Rev>0.25</From_Sticky_Rev>"
        "<Heading_Retention_Normal>0.9</Heading_Retention_Normal><Heading_Retention_Turning>0.7</Heading_Retention_Turning>"
        "</Position_Based>"
        "<camera_fov_scale>1.2</camera_fov_scale>"
        "<Max_FOV>75</Max_FOV>"
        "<Min_Follow_Dist_Multiplier>0.8</Min_Follow_Dist_Multiplier>"
        "</Vehicle></Table></root>";
    Document doc1 = P(xml1);
    CHECK(doc1.warnings().empty());
    auto rows1 = ParseVehicleCamerasTable(doc1);
    CHECK(rows1.size() == 1);
    const VehicleCameraFields& f1 = rows1[0].camera;
    CHECK(rows1[0].name.has_value() && *rows1[0].name == "Attrazione");
    CHECK(f1.cameraProximityLockY.has_value() && near(*f1.cameraProximityLockY, 2.5f, 1e-6f));
    CHECK(f1.primaryCameraAnglePresent == true);
    CHECK(f1.secondaryCameraAnglePresent == true);
    CHECK(f1.rcCameraAnglePresent == false);
    CHECK(f1.positionBasedPresent == true);
    CHECK(f1.sphereBasedPresent == false);
    CHECK(f1.positionSwingRateFwd.has_value() && near(*f1.positionSwingRateFwd, 0.3f, 1e-6f));
    CHECK(f1.positionSwingRateRev.has_value() && near(*f1.positionSwingRateRev, 0.4f, 1e-6f));
    CHECK(f1.positionFromStickyFwd.has_value() && near(*f1.positionFromStickyFwd, 0.2f, 1e-6f));
    CHECK(f1.positionFromStickyRev.has_value() && near(*f1.positionFromStickyRev, 0.25f, 1e-6f));
    CHECK(f1.PositionHeadingRetentionNormalOrDefault() > 0.89f);
    CHECK(f1.PositionHeadingRetentionTurningOrDefault() > 0.69f);
    CHECK(f1.CameraFovScaleOrDefault() > 1.19f);
    CHECK(f1.maxFov.present && near(f1.maxFov.value, 75.0f, 1e-6f));
    CHECK(f1.MinFollowDistMultiplierOrDefault() > 0.79f);
    CHECK(f1.skipCameraTransition == false);
    CHECK(f1.useAltFreckleCam == false);
    // Defaults applied when absent (spec-stated concrete defaults, 8.1).
    CHECK(near(f1.PositionHeadingRetentionNormalOrDefault(), 0.9f, 1e-3f));

    // Sphere_Based variant + Camera_Roll + CHILD ELEMENTS (CORRECTED
    // 2026-10-01, 8.1 - were previously modelled, and tested here, as row
    // attributes; see tables.h's VehicleCameraFields banner).
    const char* xml2 =
        "<root><Table><Vehicle_Group>"
        "<Name>Compacts</Name>"
        "<skip_camera_transition>yes</skip_camera_transition><use_alt_freckle_cam>yes</use_alt_freckle_cam>"
        "<Sphere_Based><Slerp_Value>0.6</Slerp_Value><From_Sticky_Slerp_Value>0.5</From_Sticky_Slerp_Value><Min_Angle>15</Min_Angle></Sphere_Based>"
        "<Camera_Roll><Roll_Type>custom_roll</Roll_Type><Intensity_Multiplier>2.0</Intensity_Multiplier></Camera_Roll>"
        "</Vehicle_Group></Table></root>";
    Document doc2 = P(xml2);
    CHECK(doc2.warnings().empty());
    auto rows2 = ParseVehicleGroupCamerasTable(doc2);
    CHECK(rows2.size() == 1);
    const VehicleCameraFields& f2 = rows2[0].camera;
    CHECK(f2.sphereBasedPresent == true);
    CHECK(f2.positionBasedPresent == false);
    CHECK(f2.sphereSlerpValue.present && near(f2.sphereSlerpValue.value, 0.6f, 1e-6f));
    CHECK(f2.sphereFromStickySlerpValue.has_value() && near(*f2.sphereFromStickySlerpValue, 0.5f, 1e-6f));
    CHECK(f2.sphereMinAngleDeg.has_value() && near(*f2.sphereMinAngleDeg, 15.0f, 1e-6f));  // RAW degrees
    CHECK(f2.cameraRollTypeText.has_value() && *f2.cameraRollTypeText == "custom_roll");
    CHECK(f2.CameraRollTypeIsCustom() == true);
    CHECK(f2.cameraRollIntensityMultiplier.has_value() && near(*f2.cameraRollIntensityMultiplier, 2.0f, 1e-6f));
    CHECK(f2.skipCameraTransition == true);
    CHECK(f2.useAltFreckleCam == true);

    // Camera_Roll/Roll_Type == "none" -> CameraRollTypeIsCustom() false (8.1's own derivation).
    const char* xml3 =
        "<root><Table><Vehicle><Name>V3</Name><Camera_Roll><Roll_Type>none</Roll_Type></Camera_Roll></Vehicle></Table></root>";
    Document doc3 = P(xml3);
    auto rows3 = ParseVehicleCamerasTable(doc3);
    CHECK(rows3.size() == 1);
    CHECK(rows3[0].camera.cameraRollTypeText.has_value() && *rows3[0].camera.cameraRollTypeText == "none");
    CHECK(rows3[0].camera.CameraRollTypeIsCustom() == false);
    CHECK(rows3[0].camera.skipCameraTransition == false);  // element absent entirely
}

// ---------------------------------------------------------------------------
// 8.2 item_cust_cameras.xtbl / player_cust_cameras_new.xtbl /
// vehicle_cust_cameras_new.xtbl (shared schema)
// ---------------------------------------------------------------------------
void testCustCameras() {
    const char* xml =
        "<root><Table><camera>"
        "<Name>placeholder</Name>"
        "<camera_view_list>"
        "<camera_view><Name>Front</Name><distance>5</distance><height>1.5</height><subj_heading>0</subj_heading>"
        "<cam_heading>180</cam_heading><pitch>10</pitch><field_of_view>45</field_of_view><snap>True</snap></camera_view>"
        "<camera_view><Name>Front</Name><distance>5</distance><height>1.5</height><subj_heading>0</subj_heading>"
        "<cam_heading>180</cam_heading><pitch>10</pitch><field_of_view>45</field_of_view><snap>False</snap><standard_def/></camera_view>"
        "</camera_view_list>"
        "</camera></Table></root>";
    Document doc = P(xml);
    CHECK(doc.warnings().empty());
    auto sets = ParseCustCamerasTable(doc);
    CHECK(sets.size() == 1);
    CHECK(sets[0].name.has_value() && *sets[0].name == "placeholder");
    CHECK(sets[0].views.size() == 2);
    CHECK(sets[0].views[0].standardDef == false);
    CHECK(sets[0].views[1].standardDef == true);  // paired HD/SD marker (8.2)
    CHECK(near(sets[0].views[0].distance.value, 5.0f, 1e-6f));
    CHECK(sets[0].views[0].snap.present && sets[0].views[0].snap.value == true);
    CHECK(sets[0].views[1].snap.present && sets[0].views[1].snap.value == false);
}

// ---------------------------------------------------------------------------
// 9. hud_qte_interface_presets.xtbl + hud_qte_interface.xtbl
// ---------------------------------------------------------------------------
void testHudQte() {
    const char* presetsXml =
        "<root><Table>"
        "<Hud_qte_preset><Name>Center</Name><X_Position>320</X_Position><Y_Position>240</Y_Position>"
        "<X_Position_SD>160</X_Position_SD><Y_Position_SD>120</Y_Position_SD></Hud_qte_preset>"
        "</Table></root>";
    Document presetsDoc = P(presetsXml);
    CHECK(presetsDoc.warnings().empty());
    auto presets = ParseHudQteInterfacePresetsTable(presetsDoc);
    CHECK(presets.size() == 1);
    CHECK(presets[0].name.has_value() && *presets[0].name == "Center");
    CHECK(presets[0].xPosition.present && presets[0].xPosition.value == 320);
    CHECK(presets[0].yPosition.present && presets[0].yPosition.value == 240);
    CHECK(presets[0].xPositionSD.present && presets[0].xPositionSD.value == 160);
    CHECK(presets[0].yPositionSD.present && presets[0].yPositionSD.value == 120);

    const char* ifaceXml =
        "<root><Table>"
        "<Hud_qte>"
        "<Name>Rapid X</Name>"
        "<Button_Type>X</Button_Type>"
        "<Button_Animation_Type>Mash Fast</Button_Animation_Type>"
        "<Button_Action>CBA_OFC_ATTACK_PRIMARY</Button_Action>"
        "<Axis_Action>CAA_AXIS_UNBOUND</Axis_Action>"  // real data's placeholder (9.2) - NOT a real table entry
        "<Axis_Dir_Pos>True</Axis_Dir_Pos>"
        "<Position>Center</Position>"
        "<Position_PC>Center</Position_PC>"
        "</Hud_qte>"
        "</Table></root>";
    Document ifaceDoc = P(ifaceXml);
    CHECK(ifaceDoc.warnings().empty());
    auto ifaces = ParseHudQteInterfaceTable(ifaceDoc);
    CHECK(ifaces.size() == 1);
    const HudQteInterface& h = ifaces[0];
    CHECK(h.name.has_value() && *h.name == "Rapid X");
    CHECK(h.buttonType == 1);              // X, index 1 in kQteButtonTypeNames
    // CORRECTED 2026-10-01 (9.2): raw text, not an EnumIndex - the real
    // table has 9 slots, only 3 names known (see tables.h's comment).
    CHECK(h.buttonAnimationType.has_value() && *h.buttonAnimationType == "Mash Fast");
    CHECK(h.buttonAction.has_value() && *h.buttonAction == "CBA_OFC_ATTACK_PRIMARY");
    CHECK(h.axisAction.has_value() && *h.axisAction == "CAA_AXIS_UNBOUND");  // kept raw, unresolved
    CHECK(h.axisDirPos.present && h.axisDirPos.value == true);
    CHECK(h.position.has_value() && *h.position == "Center");
    CHECK(h.positionPC.has_value() && *h.positionPC == "Center");
}

// ---------------------------------------------------------------------------
// 10. user_interface.xtbl
// ---------------------------------------------------------------------------
void testUserInterface() {
    const char* xml =
        "<root><Table>"
        "<UserInterface>"
        "<Name>PC_old - not used</Name>"
        "</UserInterface>"
        "<UserInterface>"
        "<Name>XBox2</Name>"
        "<ResolutionList><Resolutions>"
        "<Resolution>4:3 (640x480)</Resolution>"
        "<Resolution>16:9 (1280x720)</Resolution>"
        "</Resolutions></ResolutionList>"
        "<UIClusters><Cluster>"
        "<ClusterName>HUD_Main</ClusterName><XPosition>10</XPosition><YPosition>20</YPosition>"
        "<ClusterOffsetList><ClusterOffset><ResolutionRatio>16:9</ResolutionRatio><XOffset>1</XOffset><YOffset>2</YOffset></ClusterOffset></ClusterOffsetList>"
        "<BitmapSlots><Part>"
        "<Name>Health_Bar</Name>"
        "<SlotList><SlotOffset><ResolutionRatio>16:9</ResolutionRatio><XOffset>3</XOffset><YOffset>4</YOffset><Scale>1.0</Scale><Alpha>0.9</Alpha></SlotOffset></SlotList>"
        "<XOffset>5</XOffset><YOffset>6</YOffset><Scale>1.1</Scale><Alpha>1.0</Alpha>"
        "</Part></BitmapSlots>"
        "</Cluster></UIClusters>"
        "</UserInterface>"
        "</Table></root>";
    Document doc = P(xml);
    auto rows = ParseUserInterfaceTable(doc);
    CHECK(rows.size() == 2);
    const UserInterfaceRow* active = FindActiveUserInterfaceRow(rows);
    CHECK(active != nullptr);
    CHECK(active->name.has_value() && *active->name == "XBox2");
    CHECK(active->resolutions.size() == 2);
    CHECK(active->resolutions[0] == "4:3 (640x480)");
    CHECK(active->resolutions[1] == "16:9 (1280x720)");
    CHECK(active->clusters.size() == 1);
    const UiCluster& cl = active->clusters[0];
    CHECK(cl.clusterName.has_value() && *cl.clusterName == "HUD_Main");
    CHECK(near(cl.xPosition.value, 10.0f, 1e-6f));
    CHECK(near(cl.yPosition.value, 20.0f, 1e-6f));
    CHECK(cl.offsets.size() == 1);
    CHECK(cl.offsets[0].resolutionRatio.has_value() && *cl.offsets[0].resolutionRatio == "16:9");
    // CORRECTED 2026-10-01 (10): ClusterOffset/Part XOffset/YOffset are i32,
    // if-present (were previously modelled as float/always, before Open
    // Item 6 was closed).
    CHECK(cl.offsets[0].xOffset.has_value() && *cl.offsets[0].xOffset == 1);
    CHECK(cl.offsets[0].yOffset.has_value() && *cl.offsets[0].yOffset == 2);
    CHECK(cl.bitmapParts.size() == 1);
    const UiPart& part = cl.bitmapParts[0];
    CHECK(part.name.has_value() && *part.name == "Health_Bar");
    CHECK(part.xOffset.has_value() && *part.xOffset == 5);
    CHECK(part.scale.has_value() && near(*part.scale, 1.1f, 1e-6f));
    CHECK(part.slots.size() == 1);
    CHECK(near(part.slots[0].xOffset.value, 3.0f, 1e-6f));
    CHECK(near(part.slots[0].alpha.value, 0.9f, 1e-6f));

    // CONTROL: a row named something else must not be selected as "active".
    Document noActive = P("<root><Table><UserInterface><Name>PC_old - not used</Name></UserInterface></Table></root>");
    auto rows2 = ParseUserInterfaceTable(noActive);
    CHECK(FindActiveUserInterfaceRow(rows2) == nullptr);

    // CONTROL: absent Part/ClusterOffset XOffset/YOffset must come back
    // !has_value() (if-present, CORRECTED 2026-10-01, 10) - not a silent 0.
    const char* xmlAbsent =
        "<root><Table><UserInterface><Name>XBox2</Name>"
        "<UIClusters><Cluster><ClusterName>HUD_Main</ClusterName>"
        "<ClusterOffsetList><ClusterOffset><ResolutionRatio>16:9</ResolutionRatio></ClusterOffset></ClusterOffsetList>"
        "<BitmapSlots><Part><Name>Health_Bar</Name></Part></BitmapSlots>"
        "</Cluster></UIClusters>"
        "</UserInterface></Table></root>";
    Document docAbsent = P(xmlAbsent);
    auto rowsAbsent = ParseUserInterfaceTable(docAbsent);
    const UserInterfaceRow* activeAbsent = FindActiveUserInterfaceRow(rowsAbsent);
    CHECK(activeAbsent != nullptr);
    CHECK(!activeAbsent->clusters[0].offsets[0].xOffset.has_value());
    CHECK(!activeAbsent->clusters[0].offsets[0].yOffset.has_value());
    CHECK(!activeAbsent->clusters[0].bitmapParts[0].xOffset.has_value());
    CHECK(!activeAbsent->clusters[0].bitmapParts[0].scale.has_value());
}

// ---------------------------------------------------------------------------
// 11. control_scheme_text.xtbl
// ---------------------------------------------------------------------------
void testControlSchemeText() {
    // Platform filter (spec 11, CORRECTED 2026-10-01): only a <Control> whose
    // <Platform> text is exactly "360" or "All" (case-sensitive) survives
    // into the loaded structure; PS3 and an absent Platform are both dropped
    // at parse time. Real-shaped case: one 360, one PS3 (dropped), one All,
    // one with no <Platform> element at all (dropped).
    const char* xml =
        "<root><Table><ControlScheme>"
        "<Name>Scheme A - On Foot</Name>"
        "<Controls>"
        "<Control><Control1>R1</Control1><LocDesc>CONTROL_DESC_GRENADE</LocDesc><Platform>360</Platform></Control>"
        "<Control><Control1>L2</Control1><LocDesc>CONTROL_DESC_PRIM_ATTACK</LocDesc><Platform>PS3</Platform></Control>"
        "<Control><Control1>X</Control1><LocDesc>CONTROL_DESC_JUMP</LocDesc><Platform>All</Platform></Control>"
        "<Control><Control1>Y</Control1><LocDesc>CONTROL_DESC_SPRINT</LocDesc></Control>"
        "</Controls>"
        "</ControlScheme></Table></root>";
    Document doc = P(xml);
    CHECK(doc.warnings().empty());
    auto schemes = ParseControlSchemeTextTable(doc);
    CHECK(schemes.size() == 1);
    CHECK(schemes[0].name.has_value() && *schemes[0].name == "Scheme A - On Foot");
    CHECK(schemes[0].controls.size() == 2);  // PS3 row and no-Platform row both dropped
    CHECK(schemes[0].controls[0].control1.has_value() && *schemes[0].controls[0].control1 == "R1");
    CHECK(schemes[0].controls[0].locDesc.has_value() && *schemes[0].controls[0].locDesc == "CONTROL_DESC_GRENADE");
    CHECK(schemes[0].controls[0].platform.has_value() && *schemes[0].controls[0].platform == "360");
    CHECK(schemes[0].controls[1].control1.has_value() && *schemes[0].controls[1].control1 == "X");
    CHECK(schemes[0].controls[1].platform.has_value() && *schemes[0].controls[1].platform == "All");

    // CONTROL: a scheme whose every Control is a non-360/All platform must
    // yield an empty controls vector (filter applies, no silent fallback).
    Document allDropped = P(
        "<root><Table><ControlScheme><Name>Scheme A - On Foot</Name>"
        "<Controls><Control><Control1>Z</Control1><Platform>PS3</Platform></Control></Controls>"
        "</ControlScheme></Table></root>");
    auto schemesAllDropped = ParseControlSchemeTextTable(allDropped);
    CHECK(schemesAllDropped.size() == 1);
    CHECK(schemesAllDropped[0].controls.empty());

    // CONTROL: the real engine's documented infinite loop ("a matched
    // ControlScheme without a Controls child makes the loader re-enter the
    // same row forever", spec 11) must NOT reproduce here - this reader is a
    // tree-walking parser, not the original's index-based loop (tables.h's
    // struct banner). A Controls-less row must just finish with an empty list.
    Document noControls = P("<root><Table><ControlScheme><Name>Scheme A - On Foot</Name></ControlScheme></Table></root>");
    auto schemesNoControls = ParseControlSchemeTextTable(noControls);
    CHECK(schemesNoControls.size() == 1);
    CHECK(schemesNoControls[0].controls.empty());
}

// ---------------------------------------------------------------------------
// 12. voice_control.xtbl
// ---------------------------------------------------------------------------
void testVoiceControlBitfields() {
    const char* xml =
        "<root><Table>"
        "<Entry><Voiceline_id>101</Voiceline_id><Local_cooldown>6500</Local_cooldown><Global_cooldown>20000</Global_cooldown>"
        "<Min_delay>700</Min_delay><Max_delay>1100</Max_delay><Play_percent>60</Play_percent></Entry>"
        "</Table></root>";
    Document doc = P(xml);
    CHECK(doc.warnings().empty());
    auto entries = ParseVoiceControlTable(doc);
    CHECK(entries.size() == 1);
    const VoiceControlEntry& e = entries[0];
    CHECK(e.voicelineId.present && e.voicelineId.value == 101);
    CHECK(e.LocalCooldownField() == 6);    // 6500 / 1000 = 6, under the 63 cap
    CHECK(e.GlobalCooldownField() == 15);  // 20000 / 1000 = 20, clamped to the 15 (4-bit) cap
    CHECK(e.MinDelayField() == 10);        // 700 / 70 = 10
    CHECK(e.MaxDelayField() == 5);         // (1100 - 700) / 80 = 5, using the RAW Min_delay
    CHECK(e.PlayPercentTier() == 2);       // 60 is in [51, 76)
    CHECK(!e.priority.has_value());
    CHECK(e.PriorityOrDefault() == 10);    // spec-stated default (12)
    CHECK(e.PriorityField() == 10);
    // External_source/Play_event (CORRECTED 2026-10-01): "always" reader, not
    // if-present - this row has neither element, so both come back !present
    // with the Always<T> hazard value 0 (NOT a real engine default).
    CHECK(!e.externalSource.present);
    CHECK(!e.playEvent.present);

    // External_source/Play_event present: the "always" reader (0x00DABDF0)
    // actually reads them when the row has them (real-shaped case).
    const char* xmlExtSrc =
        "<root><Table>"
        "<Entry><Voiceline_id>55</Voiceline_id><External_source>777</External_source><Play_event>888</Play_event></Entry>"
        "</Table></root>";
    Document docExtSrc = P(xmlExtSrc);
    auto entriesExtSrc = ParseVoiceControlTable(docExtSrc);
    CHECK(entriesExtSrc.size() == 1);
    CHECK(entriesExtSrc[0].externalSource.present && entriesExtSrc[0].externalSource.value == 777);
    CHECK(entriesExtSrc[0].playEvent.present && entriesExtSrc[0].playEvent.value == 888);

    // Tier boundaries (26/51/76) and an out-of-range Priority clamp.
    const char* xml2 =
        "<root><Table>"
        "<Entry><Play_percent>0</Play_percent></Entry>"
        "<Entry><Play_percent>26</Play_percent></Entry>"
        "<Entry><Play_percent>76</Play_percent><Priority>200</Priority></Entry>"
        "</Table></root>";
    Document doc2 = P(xml2);
    auto entries2 = ParseVoiceControlTable(doc2);
    CHECK(entries2.size() == 3);
    CHECK(entries2[0].PlayPercentTier() == 0);
    CHECK(entries2[1].PlayPercentTier() == 1);
    CHECK(entries2[2].PlayPercentTier() == 3);
    CHECK(entries2[2].priority.has_value() && *entries2[2].priority == 200);
    CHECK(entries2[2].PriorityField() == 31);  // clamped to the 5-bit field's max
}

// ---------------------------------------------------------------------------
// 13. credits_pc.xtbl - the doubled tag-name shape (headings/headings, items/items)
// ---------------------------------------------------------------------------
void testCreditsDoubledTags() {
    const char* xml =
        "<root><Table><Credits>"
        "<Name>MainCredits</Name>"
        "<section_title>Development</section_title>"
        "<headings>"
        "<headings><heading>Producers</heading><type>Name - Role</type>"
        "<items><items><name>Alice</name><desc>Producer</desc></items>"
        "<items><name>Bob</name><desc>Associate Producer</desc><type_override>Centered Single Line</type_override></items></items>"
        "</headings>"
        "<headings><heading>Music</heading><type>Music</type>"
        "<items><items><name>Track One</name></items></items>"
        "</headings>"
        "</headings>"
        "</Credits></Table></root>";
    Document doc = P(xml);
    CHECK(doc.warnings().empty());
    auto sections = ParseCreditsTable(doc);
    CHECK(sections.size() == 1);
    const CreditsSection& sec = sections[0];
    CHECK(sec.name.has_value() && *sec.name == "MainCredits");
    CHECK(sec.sectionTitle.has_value() && *sec.sectionTitle == "Development");
    CHECK(sec.headings.size() == 2);
    CHECK(sec.headings[0].heading.has_value() && *sec.headings[0].heading == "Producers");
    CHECK(sec.headings[0].type == 0);  // "Name - Role"
    CHECK(sec.headings[0].items.size() == 2);
    CHECK(sec.headings[0].items[0].name.has_value() && *sec.headings[0].items[0].name == "Alice");
    CHECK(sec.headings[0].items[0].typeOverride == -1);  // absent -> unmatched sentinel
    CHECK(sec.headings[0].items[1].name.has_value() && *sec.headings[0].items[1].name == "Bob");
    CHECK(sec.headings[0].items[1].typeOverride == 3);  // "Centered Single Line"
    CHECK(sec.headings[1].heading.has_value() && *sec.headings[1].heading == "Music");
    CHECK(sec.headings[1].type == 1);  // "Music"
    CHECK(sec.headings[1].items.size() == 1);
    CHECK(sec.headings[1].items[0].name.has_value() && *sec.headings[1].items[0].name == "Track One");
}

// ---------------------------------------------------------------------------
// 14. control_filters.xtbl + control_parameters.xtbl (non-<Table> root shape)
// ---------------------------------------------------------------------------
void testControlFiltersAndParameters() {
    const char* filtersXml =
        "<root><control_filters>"
        "<control_filter><validated>True</validated><name>speed linear map filter</name><type>linear map</type>"
        "<source_name>raw speed</source_name><input_min>0</input_min><input_max>10</input_max><output_min>0</output_min><output_max>1</output_max></control_filter>"
        "<control_filter><validated>True</validated><name>speed delta multiplier filter</name><type>delta multiplier</type>"
        "<source_name>speed linear map filter</source_name><multiplier>2.5</multiplier></control_filter>"
        "<control_filter><validated>True</validated><name>speed cap filter</name><type>cap</type>"
        "<source_name>speed delta multiplier filter</source_name><output_min>-1</output_min><output_max>1</output_max></control_filter>"
        "</control_filters></root>";
    Document filtersDoc = P(filtersXml);
    CHECK(filtersDoc.warnings().empty());
    CHECK(filtersDoc.table() == nullptr);  // no <Table> element - confirms this file's non-standard shape (14)
    auto filters = ParseControlFiltersTable(filtersDoc);
    CHECK(filters.size() == 3);
    CHECK(filters[0].type.has_value() && *filters[0].type == "linear map");
    CHECK(filters[0].inputMin.has_value() && near(*filters[0].inputMin, 0.0f, 1e-6f));
    CHECK(filters[0].inputMax.has_value() && near(*filters[0].inputMax, 10.0f, 1e-6f));
    CHECK(filters[1].type.has_value() && *filters[1].type == "delta multiplier");
    CHECK(filters[1].multiplier.has_value() && near(*filters[1].multiplier, 2.5f, 1e-6f));
    CHECK(filters[1].sourceName.has_value() && *filters[1].sourceName == "speed linear map filter");
    CHECK(filters[2].type.has_value() && *filters[2].type == "cap");
    CHECK(filters[2].outputMin.has_value() && near(*filters[2].outputMin, -1.0f, 1e-6f));

    const char* paramsXml =
        "<root><control_parameters>"
        "<control_parameter><name>current speed</name><data_type>float</data_type><default_value>0.0</default_value><min_value>0</min_value><max_value>100</max_value></control_parameter>"
        "<control_parameter><name>combat ready</name><data_type>bool</data_type><default_value>false</default_value></control_parameter>"
        "</control_parameters></root>";
    Document paramsDoc = P(paramsXml);
    CHECK(paramsDoc.table() == nullptr);
    auto params = ParseControlParametersTable(paramsDoc);
    CHECK(params.size() == 2);
    CHECK(params[0].name.has_value() && *params[0].name == "current speed");
    CHECK(params[0].dataType.has_value() && *params[0].dataType == "float");
    CHECK(params[0].minValue.has_value() && near(*params[0].minValue, 0.0f, 1e-6f));
    CHECK(params[0].maxValue.has_value() && near(*params[0].maxValue, 100.0f, 1e-6f));
    CHECK(params[1].dataType.has_value() && *params[1].dataType == "bool");
    CHECK(params[1].defaultValue.has_value() && *params[1].defaultValue == "false");  // kept raw, not type-converted
    CHECK(!params[1].minValue.has_value());
}

}  // namespace

int main() {
    testLookupTables();
    testBindingSetControlAndAxis();
    testControlScheme();
    testQteGlobalParams();
    testQteSequenceAndNode();
    testVehicleCameraFields();
    testCustCameras();
    testHudQte();
    testUserInterface();
    testControlSchemeText();
    testVoiceControlBitfields();
    testCreditsDoubledTags();
    testControlFiltersAndParameters();

    std::cerr << (g_passed + g_failed) << " tests run, " << g_failed << " failed.\n";
    if (g_failed == 0) {
        std::cout << g_passed << " tests passed.\n";
        return 0;
    }
    return 1;
}
