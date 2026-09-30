// Parse functions for sr3tables_animation (see
// include/sr3tables_animation/tables.h for the struct definitions and
// per-field spec citations). Built ONLY on sr3xtbl's accessors
// (include/sr3xtbl/xtbl.h); nothing here touches the game executable,
// disassembly or decompiled code. Source: spec-tables-animation.md.

#include "sr3tables_animation/tables.h"

namespace sr3tables_animation {

namespace {

using sr3xtbl::Node;

std::optional<std::string> getText(const Node* node, std::string_view name = {}) {
    const std::string* t = sr3xtbl::ChildText(node, name);
    if (!t) return std::nullopt;
    return *t;
}

}  // namespace

// ===========================================================================
// 2. anim_set_filenames.xtbl
// ===========================================================================
AnimSetFilename ParseAnimSetFilename(const Node* row) {
    AnimSetFilename a;
    a.name = sr3xtbl::CopyText(row, "Name", 0x100);
    a.path = getText(row, "Path");
    a.defaultModel = getText(row, "Default_model");
    a.defaultRig = getText(row, "Default_rig");
    return a;
}

std::vector<AnimSetFilename> ParseAnimSetFilenamesTable(const Document& doc) {
    std::vector<AnimSetFilename> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Anim_Set_Filename"))
        out.push_back(ParseAnimSetFilename(row));
    return out;
}

// ===========================================================================
// 3. anim_files.xtbl
// ===========================================================================
namespace {

AnimFileIk parseAnimFileIk(const Node* row) {
    AnimFileIk ik;
    ik.enable = sr3xtbl::ReadBoolAlways(row, "Enable");
    ik.location = getText(row, "Location");
    ik.situation = getText(row, "Situation");
    ik.frame = sr3xtbl::ReadInt32Always(row, "Frame");
    ik.blend = sr3xtbl::ReadInt32Always(row, "Blend");
    return ik;
}

AnimFileSound parseAnimFileSound(const Node* row) {
    AnimFileSound s;
    s.frame = sr3xtbl::ReadInt32Always(row, "Frame");
    s.audioSwitch0 = getText(row, "Audio_Switch_0");
    s.audioSwitch1 = getText(row, "Audio_Switch_1");
    s.replayOnCycleRestart = sr3xtbl::ReadBoolAlways(row, "Replay_On_Cycle_Restart");
    s.stopWhenAnimStops = sr3xtbl::ReadBoolAlways(row, "Stop_When_Anim_Stops");
    s.audioEvent = getText(row, "Audio_Event");
    return s;
}

}  // namespace

AnimFile ParseAnimFile(const Node* row) {
    AnimFile f;
    const Node* animation = sr3xtbl::FindChild(row, "Animation");
    f.filename = getText(animation, "Filename");
    f.preload = sr3xtbl::ReadBoolAlways(animation, "Preload");

    const Node* triggers = sr3xtbl::FindChild(row, "Triggers");
    for (const Node* t : sr3xtbl::Children(triggers, "Trigger")) {
        if (auto n = getText(t, "Name")) f.triggerNames.push_back(*n);
    }

    const Node* iks = sr3xtbl::FindChild(row, "IKs");
    for (const Node* ik : sr3xtbl::Children(iks, "IK")) f.iks.push_back(parseAnimFileIk(ik));

    const Node* sounds = sr3xtbl::FindChild(row, "Sounds");
    for (const Node* s : sr3xtbl::Children(sounds, "Sound")) f.sounds.push_back(parseAnimFileSound(s));

    f.voiceLinesPresent = sr3xtbl::FindChild(row, "Voice_Lines") != nullptr;
    f.miscPresent = sr3xtbl::FindChild(row, "misc") != nullptr;

    const Node* flags = sr3xtbl::FindChild(row, "Flags");
    f.flags = sr3xtbl::FlagMask(flags, kAnimFileFlagNames.data(), kAnimFileFlagNames.size());

    return f;
}

std::vector<AnimFile> ParseAnimFilesTable(const Document& doc) {
    std::vector<AnimFile> out;
    const Node* files = sr3xtbl::FindChild(doc.table(), "Files");
    const Node* animFiles = sr3xtbl::FindChild(files, "Anim_files");
    for (const Node* row : sr3xtbl::Children(animFiles, "Anim_file")) out.push_back(ParseAnimFile(row));
    return out;
}

// ===========================================================================
// 4. anim_groups.xtbl
// ===========================================================================
AnimGroup ParseAnimGroup(const Node* row) {
    AnimGroup g;
    g.name = getText(row, "Name");
    g.parent = getText(row, "Parent");
    return g;
}

std::vector<AnimGroup> ParseAnimGroupsTable(const Document& doc) {
    std::vector<AnimGroup> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Anim_Group")) out.push_back(ParseAnimGroup(row));
    return out;
}

// ===========================================================================
// 5. anim_states.xtbl / anim_actions.xtbl
// ===========================================================================
AnimState ParseAnimState(const Node* row) {
    AnimState s;
    s.name = getText(row, "Name");
    return s;
}

std::vector<AnimState> ParseAnimStatesTable(const Document& doc) {
    std::vector<AnimState> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "State")) out.push_back(ParseAnimState(row));
    return out;
}

AnimAction ParseAnimAction(const Node* row) {
    AnimAction a;
    a.name = getText(row, "Name");
    return a;
}

std::vector<AnimAction> ParseAnimActionsTable(const Document& doc) {
    std::vector<AnimAction> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Action")) out.push_back(ParseAnimAction(row));
    return out;
}

// ===========================================================================
// 6. anim_transitions.xtbl
// ===========================================================================
AnimTransition ParseAnimTransition(const Node* row) {
    AnimTransition t;
    t.fromState = getText(row, "From_State");
    t.toState = getText(row, "To_State");
    t.action = getText(row, "Action");
    return t;
}

std::vector<AnimTransition> ParseAnimTransitionsTable(const Document& doc) {
    std::vector<AnimTransition> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Anim_Transition")) out.push_back(ParseAnimTransition(row));
    return out;
}

// ===========================================================================
// 7. anim_blend_trees.xtbl
// ===========================================================================
namespace {

AnimBlendTreeControlPoint parseAnimBlendTreeControlPoint(const Node* row) {
    AnimBlendTreeControlPoint c;
    c.range = sr3xtbl::GetFloat(row, "Range");
    c.value = sr3xtbl::GetFloat(row, "Value");
    return c;
}

AnimBlendTreeState parseAnimBlendTreeState(const Node* row) {
    AnimBlendTreeState s;
    s.animationFilename = getText(sr3xtbl::FindChild(row, "Animation"), "Filename");
    s.blendTime = sr3xtbl::GetFloat(row, "Blend_time");
    const Node* controlPoints = sr3xtbl::FindChild(row, "Control_points");
    for (const Node* cp : sr3xtbl::Children(controlPoints, "Control_point"))
        s.controlPoints.push_back(parseAnimBlendTreeControlPoint(cp));
    return s;
}

}  // namespace

AnimBlendTree ParseAnimBlendTree(const Node* row) {
    AnimBlendTree b;
    b.name = getText(row, "Name");
    b.controlInputLow = sr3xtbl::GetFloat(row, "Control_input_low");
    b.controlInputHigh = sr3xtbl::GetFloat(row, "Control_input_high");
    b.rampSpeed = sr3xtbl::GetFloat(row, "Ramp_speed");
    const Node* states = sr3xtbl::FindChild(row, "States");
    for (const Node* s : sr3xtbl::Children(states, "State")) b.states.push_back(parseAnimBlendTreeState(s));
    return b;
}

std::vector<AnimBlendTree> ParseAnimBlendTreesTable(const Document& doc) {
    std::vector<AnimBlendTree> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Blend_trees")) out.push_back(ParseAnimBlendTree(row));
    return out;
}

// ===========================================================================
// 8. anim_ik_situation.xtbl
// ===========================================================================
AnimIkSituation ParseAnimIkSituation(const Node* row) {
    AnimIkSituation s;
    s.name = getText(row, "Name");
    s.propName = getText(row, "Prop_Name");
    s.morphedSurfaceMaxAdjustment = sr3xtbl::ReadFloatAlways(row, "Morphed_Surface_Max_Adjustment");
    const Node* flags = sr3xtbl::FindChild(row, "Flags");
    s.flags = sr3xtbl::FlagMask(flags, kAnimIkSituationFlagNames.data(), kAnimIkSituationFlagNames.size());
    return s;
}

std::vector<AnimIkSituation> ParseAnimIkSituationsTable(const Document& doc) {
    std::vector<AnimIkSituation> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "IK_Situation")) out.push_back(ParseAnimIkSituation(row));
    return out;
}

// ===========================================================================
// 9. anim_triggers.xtbl
// ===========================================================================
AnimTrigger ParseAnimTrigger(const Node* row) {
    AnimTrigger t;
    t.name = sr3xtbl::CopyText(row, "Name", 0x20);
    return t;
}

std::vector<AnimTrigger> ParseAnimTriggersTable(const Document& doc) {
    std::vector<AnimTrigger> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Trigger")) out.push_back(ParseAnimTrigger(row));
    return out;
}

// ===========================================================================
// 10. anim_flinches.xtbl
// ===========================================================================
uint8_t ResolveFlinchHitLocation(std::string_view text) {
    for (const AnimFlinchNamedValue& e : kAnimFlinchHitLocationTable)
        if (sr3xtbl::NameEquals(text, e.name)) return e.value;
    return 0;
}

AnimFlinch ParseAnimFlinch(const Node* row) {
    AnimFlinch f;
    f.name = getText(row, "Name");
    f.hitLocation = getText(row, "Hit_Location");
    f.hitDirection = getText(row, "Hit_Direction");
    f.type = getText(row, "Type");
    const Node* flags = sr3xtbl::FindChild(row, "Flags");
    f.flags = sr3xtbl::FlagMask(flags, kAnimFlinchFlagNames.data(), kAnimFlinchFlagNames.size());
    return f;
}

std::vector<AnimFlinch> ParseAnimFlinchesTable(const Document& doc) {
    std::vector<AnimFlinch> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Flinch")) out.push_back(ParseAnimFlinch(row));
    return out;
}

// ===========================================================================
// 11. anim_synced.xtbl
// ===========================================================================
AnimSyncedMove ParseAnimSyncedMove(const Node* row) {
    AnimSyncedMove m;
    m.attackerAnim = getText(sr3xtbl::FindChild(row, "AttackerAnim"), "Filename");
    m.victimAnim = getText(sr3xtbl::FindChild(row, "VictimAnim"), "Filename");
    const Node* victimOffsets = sr3xtbl::FindChild(row, "VictimOffsets");
    m.xOffset = sr3xtbl::ReadFloatAlways(victimOffsets, "XOffset");
    m.yOffset = sr3xtbl::GetFloat(victimOffsets, "YOffset");
    m.zOffset = sr3xtbl::ReadFloatAlways(victimOffsets, "ZOffset");
    m.heading = sr3xtbl::ReadFloatAlways(victimOffsets, "heading");
    const Node* flags = sr3xtbl::FindChild(row, "Flags");
    m.flags = sr3xtbl::FlagMask(flags, kAnimSyncedFlagNames.data(), kAnimSyncedFlagNames.size());
    return m;
}

std::vector<AnimSyncedMove> ParseAnimSyncedTable(const Document& doc) {
    std::vector<AnimSyncedMove> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "SyncedMove")) out.push_back(ParseAnimSyncedMove(row));
    return out;
}

// ===========================================================================
// 12. anim_correction_offsets.xtbl
// ===========================================================================
AnimCorrectionOffset ParseAnimCorrectionOffset(const Node* row) {
    AnimCorrectionOffset c;
    c.fileFilename = getText(sr3xtbl::FindChild(row, "File"), "Filename");
    c.offset = sr3xtbl::ReadVec3Child(row, "Offset");
    return c;
}

std::vector<AnimCorrectionOffset> ParseAnimCorrectionOffsetsTable(const Document& doc) {
    std::vector<AnimCorrectionOffset> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Correction_Offset"))
        out.push_back(ParseAnimCorrectionOffset(row));
    return out;
}

// ===========================================================================
// 13. creation_lipsync_animations.xtbl
// ===========================================================================
namespace {

LipsyncChance parseLipsyncChance(const Node* row) {
    LipsyncChance c;
    c.trigger = getText(row, "Trigger");
    c.variant = sr3xtbl::GetUInt16(row, "Variant");
    c.animation = getText(row, "Animation");
    return c;
}

}  // namespace

LipsyncEntry ParseLipsyncEntry(const Node* row) {
    LipsyncEntry e;
    e.name = getText(row, "Name");
    const Node* chances = sr3xtbl::FindChild(row, "Chances");
    for (const Node* c : sr3xtbl::Children(chances, "Chance")) e.chances.push_back(parseLipsyncChance(c));
    return e;
}

std::vector<LipsyncEntry> ParseCreationLipsyncAnimationsTable(const Document& doc) {
    std::vector<LipsyncEntry> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Entry")) out.push_back(ParseLipsyncEntry(row));
    return out;
}

// ===========================================================================
// 14. character_visemes.xtbl
// ===========================================================================
namespace {

VisemeTarget parseVisemeTarget(const Node* row) {
    VisemeTarget t;
    t.targetName = getText(row, "Target_Name");
    t.targetValue = sr3xtbl::ReadFloatAlways(row, "Target_Value");
    return t;
}

}  // namespace

Viseme ParseViseme(const Node* row) {
    Viseme v;
    v.name = getText(row, "Name");
    const Node* targets = sr3xtbl::FindChild(row, "Targets");
    for (const Node* t : sr3xtbl::Children(targets, "Target")) v.targets.push_back(parseVisemeTarget(t));
    return v;
}

std::vector<Viseme> ParseCharacterVisemesTable(const Document& doc) {
    std::vector<Viseme> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Viseme")) out.push_back(ParseViseme(row));
    return out;
}

}  // namespace sr3tables_animation
