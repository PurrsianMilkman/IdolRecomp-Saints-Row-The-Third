// Synthetic tests for sr3tables_animation, built FROM THE TEXT of
// spec-tables-animation.md only (hand-rolled XML fixtures as string
// literals) - never derived from this project's own reader source. A pass
// here proves the reader implements the spec's text, not that the spec is
// right; the real-data statement is
// tools/validation/validate_tables_animation_population.cpp.
//
// One test function per implemented table (14; anim_set_properties.xtbl and
// anim_prop_sets.xtbl are skipped entirely, see tables.h). Each covers at
// least: a normal row, an absent optional element giving nullopt/present==
// false, and, where the spec calls one out, a boundary/quirk value
// (anim_triggers.xtbl's 31-char bound, anim_flinches.xtbl's non-sequential
// Hit_Location value table and its documented "Male" dead-flag finding,
// anim_synced.xtbl's YOffset-differs-from-its-siblings presence behaviour,
// anim_transitions.xtbl's base-game-empty-<Table> quirk).

#include <iostream>
#include <string>
#include <vector>

#include "sr3tables_animation/tables.h"

namespace {

using namespace sr3tables_animation;
using namespace sr3xtbl;

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                               \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(cond)) {                                                            \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__   \
                      << "\n";                                                    \
            ++g_failures;                                                         \
        }                                                                         \
    } while (0)

Document P(std::string_view s) { return ParseDocument(s); }

bool near(float a, float b, float tol) { return (a > b ? a - b : b - a) <= tol; }

// ===========================================================================
// 2. anim_set_filenames.xtbl
// ===========================================================================
void testAnimSetFilename() {
    Document d = P(R"(<root><Table>
      <Anim_Set_Filename><Name>anim_auto.xtbl</Name><Path>\vehicles\automobiles\base</Path>
        <Default_model>cm_body.cmeshx</Default_model><Default_rig>cm_body.rigx</Default_rig></Anim_Set_Filename>
      <Anim_Set_Filename><Name>anim_jet.xtbl</Name></Anim_Set_Filename>
    </Table></root>)");
    std::vector<AnimSetFilename> rows = ParseAnimSetFilenamesTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name.has_value() && *rows[0].name == "anim_auto.xtbl");
    CHECK(rows[0].path.has_value() && *rows[0].path == "\\vehicles\\automobiles\\base");
    CHECK(rows[0].defaultModel.has_value() && *rows[0].defaultModel == "cm_body.cmeshx");
    CHECK(rows[0].defaultRig.has_value() && *rows[0].defaultRig == "cm_body.rigx");
    // Minimal row: unread-by-the-loader fields absent.
    CHECK(rows[1].name.has_value() && *rows[1].name == "anim_jet.xtbl");
    CHECK(!rows[1].path.has_value() && !rows[1].defaultModel.has_value() && !rows[1].defaultRig.has_value());
}

// ===========================================================================
// 3. anim_files.xtbl
// ===========================================================================
void testAnimFile() {
    Document d = P(R"(<root><Table><Files><Anim_files>
      <Anim_file>
        <Animation><Filename>plyr_idle.animx</Filename><Preload>True</Preload></Animation>
        <Triggers><Trigger><Name>footstep_l</Name></Trigger><Trigger><Name>footstep_r</Name></Trigger></Triggers>
        <IKs><IK><Enable>True</Enable><Location>Left Foot</Location><Situation>ladder_grab</Situation>
          <Frame>12</Frame><Blend>4</Blend></IK></IKs>
        <Sounds><Sound><Frame>5</Frame><Audio_Switch_0>foley:step</Audio_Switch_0><Audio_Switch_1>foley:concrete</Audio_Switch_1>
          <Replay_On_Cycle_Restart>True</Replay_On_Cycle_Restart><Stop_When_Anim_Stops>False</Stop_When_Anim_Stops>
          <Audio_Event>evt_step</Audio_Event></Sound></Sounds>
        <Voice_Lines></Voice_Lines>
        <misc><Custom>1</Custom></misc>
        <Flags><Flag>Additive</Flag><Flag>Cloth Sim Hack</Flag><Flag>Transition to 50%</Flag></Flags>
      </Anim_file>
      <Anim_file><Animation><Filename>minimal.animx</Filename></Animation></Anim_file>
    </Anim_files></Files></Table></root>)");
    std::vector<AnimFile> rows = ParseAnimFilesTable(d);
    CHECK(rows.size() == 2);
    const AnimFile& f0 = rows[0];
    CHECK(f0.filename.has_value() && *f0.filename == "plyr_idle.animx");
    CHECK(f0.preload.present && f0.preload.value == true);
    CHECK(f0.triggerNames.size() == 2 && f0.triggerNames[0] == "footstep_l" && f0.triggerNames[1] == "footstep_r");
    CHECK(f0.iks.size() == 1);
    CHECK(f0.iks[0].enable.value == true);
    CHECK(f0.iks[0].location.has_value() && *f0.iks[0].location == "Left Foot");
    // kAnimFileIkLocationNames (spec 3.2): index == the engine's own value;
    // "Left Foot" is index 2.
    {
        const Node* ikNode = FindChild(FindChild(FindChild(d.table(), "Files"), "Anim_files"), "Anim_file");
        ikNode = FindChild(FindChild(ikNode, "IKs"), "IK");
        CHECK(EnumIndex(FindChild(ikNode, "Location"), kAnimFileIkLocationNames.data(),
                         kAnimFileIkLocationNames.size()) == 2);
    }
    CHECK(f0.iks[0].situation.has_value() && *f0.iks[0].situation == "ladder_grab");
    CHECK(f0.iks[0].frame.value == 12 && f0.iks[0].blend.value == 4);
    CHECK(f0.sounds.size() == 1);
    CHECK(f0.sounds[0].audioSwitch0.has_value() && *f0.sounds[0].audioSwitch0 == "foley:step");
    CHECK(f0.sounds[0].replayOnCycleRestart.value == true && f0.sounds[0].stopWhenAnimStops.value == false);
    CHECK(f0.voiceLinesPresent);   // element present (even though empty) - documents the "doubly-dead" finding
    CHECK(f0.miscPresent);
    // Flags: bit 0 (Additive) + bit 29 (Cloth Sim Hack) + bit 27 (Transition to 50%).
    CHECK((f0.flags & (1u << 0)) != 0);
    CHECK((f0.flags & (1u << 29)) != 0);
    CHECK((f0.flags & (1u << 27)) != 0);
    CHECK((f0.flags & (1u << 1)) == 0);  // Block_Rotation NOT set

    const AnimFile& f1 = rows[1];
    CHECK(f1.filename.has_value() && *f1.filename == "minimal.animx");
    CHECK(!f1.preload.present);
    CHECK(f1.triggerNames.empty() && f1.iks.empty() && f1.sounds.empty());
    CHECK(!f1.voiceLinesPresent && !f1.miscPresent);
    CHECK(f1.flags == 0);
    CHECK(f0.triggerElements == 2 && f0.triggerElementsUnread == 0);
    CHECK(f1.triggerElements == 0 && f1.triggerElementsUnread == 0);

    // Trigger shape is OPEN (spec 3.1 direct text vs 3.2 Name child): a
    // direct-text Trigger is not read as a name, but it is counted, never
    // dropped silently.
    Document d2 = P(R"(<root><Table><Files><Anim_files><Anim_file>
        <Triggers><Trigger>text_shaped</Trigger><Trigger><Name>named</Name></Trigger><Trigger/></Triggers>
      </Anim_file></Anim_files></Files></Table></root>)");
    std::vector<AnimFile> rows2 = ParseAnimFilesTable(d2);
    CHECK(rows2.size() == 1);
    CHECK(rows2[0].triggerElements == 3);
    CHECK(rows2[0].triggerNames.size() == 1 && rows2[0].triggerNames[0] == "named");
    CHECK(rows2[0].triggerElementsUnread == 2);
}

// ===========================================================================
// 4. anim_groups.xtbl
// ===========================================================================
void testAnimGroup() {
    Document d = P(R"(<root><Table>
      <Anim_Group><Name>vehicle_base</Name></Anim_Group>
      <Anim_Group><Name>vehicle_auto</Name><Parent>vehicle_base</Parent></Anim_Group>
    </Table></root>)");
    std::vector<AnimGroup> rows = ParseAnimGroupsTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name.has_value() && *rows[0].name == "vehicle_base" && !rows[0].parent.has_value());
    CHECK(rows[1].parent.has_value() && *rows[1].parent == "vehicle_base");
}

// ===========================================================================
// 5. anim_states.xtbl / anim_actions.xtbl
// ===========================================================================
void testAnimStatesAndActions() {
    Document ds = P("<root><Table><State><Name>idle</Name></State><State><Name>run</Name></State></Table></root>");
    std::vector<AnimState> states = ParseAnimStatesTable(ds);
    CHECK(states.size() == 2 && states[0].name == "idle" && states[1].name == "run");

    Document da = P(R"(<root><Table>
      <Action><Name>jump</Name><_Editor><Category>Locomotion</Category></_Editor></Action>
    </Table></root>)");
    std::vector<AnimAction> actions = ParseAnimActionsTable(da);
    CHECK(actions.size() == 1 && actions[0].name.has_value() && *actions[0].name == "jump");
}

// ===========================================================================
// 6. anim_transitions.xtbl
// ===========================================================================
void testAnimTransition() {
    Document d = P(R"(<root><Table>
      <Anim_Transition><From_State>idle</From_State><To_State>run</To_State><Action>start_run</Action></Anim_Transition>
    </Table></root>)");
    std::vector<AnimTransition> rows = ParseAnimTransitionsTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].fromState == "idle" && rows[0].toState == "run" && rows[0].action == "start_run");

    // The base-game quirk (spec 6.3): an empty <Table> with authored rows
    // parked under the sibling <TableTemplates> - the real loader (and this
    // reader, matching it) never visits TableTemplates, so parsing this
    // shape MUST yield zero rows, not two.
    Document baseGameShape = P(R"(<root>
      <Table>
      </Table>
      <TableTemplates>
        <Anim_Transition><From_State>idle</From_State><To_State>run</To_State><Action>start_run</Action></Anim_Transition>
      </TableTemplates>
    </root>)");
    std::vector<AnimTransition> baseRows = ParseAnimTransitionsTable(baseGameShape);
    CHECK(baseRows.empty());
}

// ===========================================================================
// 7. anim_blend_trees.xtbl
// ===========================================================================
void testAnimBlendTree() {
    // Root/row shape quirk (spec 7.1): the row tag repeats the root-fetch
    // name, so rows are direct children of <Table>, not nested in a plural
    // container.
    Document d = P(R"(<root><Table>
      <Blend_trees><Name>locomotion</Name><Control_input_low>0</Control_input_low><Control_input_high>10</Control_input_high>
        <Ramp_speed>2.5</Ramp_speed>
        <States><State><Animation><Filename>walk.animx</Filename></Animation><Blend_time>0.25</Blend_time>
          <Control_points><Control_point><Range>0</Range><Value>0</Value></Control_point>
            <Control_point><Range>5</Range><Value>1</Value></Control_point></Control_points></State>
        </States>
      </Blend_trees>
      <Blend_trees><Name>minimal</Name></Blend_trees>
    </Table></root>)");
    std::vector<AnimBlendTree> rows = ParseAnimBlendTreesTable(d);
    CHECK(rows.size() == 2);
    const AnimBlendTree& b0 = rows[0];
    CHECK(b0.name.has_value() && *b0.name == "locomotion");
    CHECK(b0.controlInputLow.has_value() && *b0.controlInputLow == 0.0f);
    CHECK(b0.controlInputHigh.has_value() && *b0.controlInputHigh == 10.0f);
    CHECK(b0.rampSpeed.has_value() && near(*b0.rampSpeed, 2.5f, 1e-6f));
    CHECK(b0.states.size() == 1);
    CHECK(b0.states[0].animationFilename.has_value() && *b0.states[0].animationFilename == "walk.animx");
    CHECK(b0.states[0].blendTime.has_value() && near(*b0.states[0].blendTime, 0.25f, 1e-6f));
    CHECK(b0.states[0].controlPoints.size() == 2);
    CHECK(b0.states[0].controlPoints[1].range.has_value() && *b0.states[0].controlPoints[1].range == 5.0f);
    CHECK(b0.states[0].controlPoints[1].value.has_value() && *b0.states[0].controlPoints[1].value == 1.0f);

    const AnimBlendTree& b1 = rows[1];
    CHECK(!b1.controlInputLow.has_value() && !b1.rampSpeed.has_value());
    CHECK(b1.states.empty());
}

// ===========================================================================
// 8. anim_ik_situation.xtbl
// ===========================================================================
void testAnimIkSituation() {
    Document d = P(R"(<root><Table>
      <IK_Situation><Name>ladder_grab</Name><Prop_Name>ladder_rung</Prop_Name>
        <Morphed_Surface_Max_Adjustment>0.5</Morphed_Surface_Max_Adjustment>
        <Flags><Flag>Two Person</Flag><Flag>Morphed Surface</Flag></Flags></IK_Situation>
      <IK_Situation><Name>minimal</Name></IK_Situation>
    </Table></root>)");
    std::vector<AnimIkSituation> rows = ParseAnimIkSituationsTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name == "ladder_grab");
    CHECK(rows[0].propName.has_value() && *rows[0].propName == "ladder_rung");
    CHECK(rows[0].morphedSurfaceMaxAdjustment.present && rows[0].morphedSurfaceMaxAdjustment.value == 0.5f);
    CHECK((rows[0].flags & (1u << 1)) != 0);  // Two Person
    CHECK((rows[0].flags & (1u << 2)) != 0);  // Morphed Surface
    CHECK((rows[0].flags & (1u << 0)) == 0);  // Hard Coded NOT set

    CHECK(!rows[1].propName.has_value());
    CHECK(!rows[1].morphedSurfaceMaxAdjustment.present && rows[1].morphedSurfaceMaxAdjustment.value == 0.0f);
    CHECK(rows[1].flags == 0);
}

// ===========================================================================
// 9. anim_triggers.xtbl
// ===========================================================================
void testAnimTrigger() {
    Document d = P(R"(<root><Table>
      <Trigger><Name>footstep</Name></Trigger>
      <Trigger><Name>this_name_is_exactly_forty_chars_long_xx</Name></Trigger>
    </Table></root>)");
    std::vector<AnimTrigger> rows = ParseAnimTriggersTable(d);
    CHECK(rows.size() == 2);
    CHECK(rows[0].name.has_value() && *rows[0].name == "footstep");
    // BOUNDARY (spec 9.1): bounded copy of at most 31 chars + NUL (buffer
    // size 0x20); a 40-char name is truncated to the first 31 characters.
    CHECK(rows[1].name.has_value() && rows[1].name->size() == 31);
    CHECK(*rows[1].name == std::string("this_name_is_exactly_forty_char"));
}

// ===========================================================================
// 10. anim_flinches.xtbl
// ===========================================================================
void testAnimFlinch() {
    Document d = P(R"(<root><Table>
      <Flinch><Name>hit_head</Name><Hit_Location>Head</Hit_Location><Hit_Direction>front</Hit_Direction>
        <Type>Heavy</Type><Flags><Flag>Crouch</Flag><Flag>Skydive</Flag></Flags></Flinch>
      <Flinch><Name>hit_dead_data</Name><Hit_Location>Pelvis</Hit_Location><Hit_Direction>left</Hit_Direction>
        <Type>Riot2</Type>
        <Flags><Flag>Cover</Flag><Flag>Male</Flag></Flags></Flinch>
      <Flinch><Name>unknown_loc</Name><Hit_Location>Nonexistent</Hit_Location></Flinch>
    </Table></root>)");
    std::vector<AnimFlinch> rows = ParseAnimFlinchesTable(d);
    CHECK(rows.size() == 3);
    CHECK(rows[0].hitLocation.has_value() && *rows[0].hitLocation == "Head");
    CHECK(ResolveFlinchHitLocation(*rows[0].hitLocation) == 6);
    {
        const Node* row0 = FindChild(d.table(), "Flinch");
        CHECK(EnumIndex(FindChild(row0, "Hit_Direction"), kAnimFlinchHitDirectionNames.data(),
                         kAnimFlinchHitDirectionNames.size()) == 0);  // "front" -> 0
    }
    CHECK((rows[0].flags & (1u << 0)) != 0);  // Crouch
    CHECK((rows[0].flags & (1u << 2)) != 0);  // Skydive
    CHECK((rows[0].flags & (1u << 1)) == 0);  // Cover NOT set

    CHECK(ResolveFlinchHitLocation(*rows[1].hitLocation) == 0xB);
    // spec 10.4's documented dead-flag finding: "Male" matches none of the
    // three recognised flag names, so it contributes nothing to the mask -
    // only Cover's bit is set.
    CHECK(rows[1].flags == (1u << 1));

    // Unrecognised Hit_Location text -> ResolveFlinchHitLocation's
    // documented zero-default (spec 10.1: "unrecognised text leaves the
    // field at its zero default").
    CHECK(ResolveFlinchHitLocation(*rows[2].hitLocation) == 0);
    CHECK(!rows[2].hitDirection.has_value());
    CHECK(rows[2].flags == 0);
}

// ===========================================================================
// 11. anim_synced.xtbl
// ===========================================================================
void testAnimSyncedMove() {
    Document d = P(R"(<root><Table>
      <SyncedMove><AttackerAnim><Filename>atk.animx</Filename></AttackerAnim>
        <VictimAnim><Filename>vic.animx</Filename></VictimAnim>
        <VictimOffsets><XOffset>1.0</XOffset><YOffset>2.0</YOffset><ZOffset>3.0</ZOffset><heading>90.0</heading></VictimOffsets>
        <Flags><Flag>attacker is brute</Flag><Flag>hold last frame</Flag></Flags></SyncedMove>
      <SyncedMove><AttackerAnim><Filename>atk2.animx</Filename></AttackerAnim>
        <VictimAnim><Filename>vic2.animx</Filename></VictimAnim>
        <VictimOffsets><XOffset>0</XOffset><ZOffset>0</ZOffset><heading>0</heading></VictimOffsets></SyncedMove>
    </Table></root>)");
    std::vector<AnimSyncedMove> rows = ParseAnimSyncedTable(d);
    CHECK(rows.size() == 2);
    const AnimSyncedMove& m0 = rows[0];
    CHECK(m0.attackerAnim.has_value() && *m0.attackerAnim == "atk.animx");
    CHECK(m0.victimAnim.has_value() && *m0.victimAnim == "vic.animx");
    CHECK(m0.xOffset.value == 1.0f && m0.zOffset.value == 3.0f && m0.heading.value == 90.0f);
    CHECK(m0.yOffset.has_value() && *m0.yOffset == 2.0f);
    CHECK((m0.flags & (1u << 5)) != 0);  // "attacker is brute"
    CHECK((m0.flags & (1u << 9)) != 0);  // "hold last frame"
    CHECK((m0.flags & (1u << 0)) == 0);

    // YOffset is the one field in this row that differs from its siblings
    // (spec 11.2): "if present", not "always" - absent here, so nullopt,
    // while X/Z/heading (Always-hazard) fall back to their 0 stand-in.
    const AnimSyncedMove& m1 = rows[1];
    CHECK(!m1.yOffset.has_value());
    CHECK(m1.flags == 0);
}

// ===========================================================================
// 12. anim_correction_offsets.xtbl
// ===========================================================================
void testAnimCorrectionOffset() {
    // Real sample shape (spec 12.4): the reader is tag-agnostic, but every
    // real row uses <Correction_Offset>, which is what this reader targets.
    Document d = P(R"(<root><Table>
      <Correction_Offset><Name>fence 05m</Name>
        <Offset><X>0.0</X><Y>-0.8185</Y><Z>-0.510</Z></Offset>
        <_Editor><Category>Entries</Category></_Editor>
        <File><Filename>plym_fnce_half_entr.animx</Filename></File>
      </Correction_Offset>
    </Table></root>)");
    std::vector<AnimCorrectionOffset> rows = ParseAnimCorrectionOffsetsTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].fileFilename.has_value() && *rows[0].fileFilename == "plym_fnce_half_entr.animx");
    CHECK(rows[0].offset.present);
    CHECK(rows[0].offset.x.value == 0.0f);
    CHECK(near(rows[0].offset.y.value, -0.8185f, 1e-4f));
    CHECK(near(rows[0].offset.z.value, -0.510f, 1e-4f));
}

// ===========================================================================
// 13. creation_lipsync_animations.xtbl
// ===========================================================================
void testLipsyncEntry() {
    Document d = P(R"(<root><Table>
      <Entry><Name>idle_talk</Name>
        <Chances>
          <Chance><Trigger>sfx_talk_a</Trigger><Variant>2</Variant><Animation>talk_gesture_a</Animation></Chance>
          <Chance><Trigger>sfx_talk_b</Trigger><Animation>talk_gesture_b</Animation></Chance>
        </Chances>
      </Entry>
    </Table></root>)");
    std::vector<LipsyncEntry> rows = ParseCreationLipsyncAnimationsTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].name.has_value() && *rows[0].name == "idle_talk");
    CHECK(rows[0].chances.size() == 2);
    CHECK(rows[0].chances[0].trigger.has_value() && *rows[0].chances[0].trigger == "sfx_talk_a");
    CHECK(rows[0].chances[0].variant.has_value() && *rows[0].chances[0].variant == 2u);
    CHECK(rows[0].chances[0].animation.has_value() && *rows[0].chances[0].animation == "talk_gesture_a");
    // Variant is "if present" - absent in the second Chance.
    CHECK(!rows[0].chances[1].variant.has_value());
}

// ===========================================================================
// 14. character_visemes.xtbl
// ===========================================================================
void testViseme() {
    Document d = P(R"(<root><Table>
      <Viseme><Name>viseme_AA</Name>
        <Targets><Target><Target_Name>jaw_open</Target_Name><Target_Value>0.8</Target_Value></Target>
          <Target><Target_Name>lip_corner_pull</Target_Name><Target_Value>0.3</Target_Value></Target></Targets>
      </Viseme>
    </Table></root>)");
    std::vector<Viseme> rows = ParseCharacterVisemesTable(d);
    CHECK(rows.size() == 1);
    CHECK(rows[0].name.has_value() && *rows[0].name == "viseme_AA");
    CHECK(rows[0].targets.size() == 2);
    CHECK(rows[0].targets[0].targetName.has_value() && *rows[0].targets[0].targetName == "jaw_open");
    CHECK(rows[0].targets[0].targetValue.present && near(rows[0].targets[0].targetValue.value, 0.8f, 1e-6f));
    CHECK(near(rows[0].targets[1].targetValue.value, 0.3f, 1e-6f));
}

}  // namespace

int main() {
    try {
        testAnimSetFilename();
        testAnimFile();
        testAnimGroup();
        testAnimStatesAndActions();
        testAnimTransition();
        testAnimBlendTree();
        testAnimIkSituation();
        testAnimTrigger();
        testAnimFlinch();
        testAnimSyncedMove();
        testAnimCorrectionOffset();
        testLipsyncEntry();
        testViseme();
    } catch (const std::exception& e) {
        std::cerr << "unexpected exception: " << e.what() << "\n";
        return 1;
    }
    if (g_failures) {
        std::cerr << g_failures << " of " << g_checks << " check(s) failed\n";
        return 1;
    }
    std::cout << g_checks << " tests passed.\n";
    return 0;
}
