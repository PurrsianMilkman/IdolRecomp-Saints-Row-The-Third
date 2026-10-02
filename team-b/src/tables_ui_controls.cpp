// Parse functions for sr3tables_ui_controls (see
// include/sr3tables_ui_controls/tables.h for the struct definitions, the
// per-field spec citations, and the file banner's JUDGEMENT CALLS /
// deliberate-gaps notes). Built ONLY on sr3xtbl's accessors
// (include/sr3xtbl/xtbl.h); nothing here touches the game executable,
// disassembly or decompiled code. Source: spec-tables-ui-controls.md.

#include "sr3tables_ui_controls/tables.h"

namespace sr3tables_ui_controls {

using namespace sr3xtbl;

namespace {

std::optional<std::string> getText(const Node* node, std::string_view name = {}) {
    const std::string* t = ChildText(node, name);
    if (!t) return std::nullopt;
    return *t;
}

template <class T>
T clampT(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

}  // namespace

// ---------------------------------------------------------------------------
// 4. Shared vocabulary tables
// ---------------------------------------------------------------------------
int32_t LookupNamedValue(const Node* node, const NamedValue* table, size_t count) {
    if (!node || !node->text()) return -1;
    for (size_t i = 0; i < count; ++i)
        if (NameEquals(*node->text(), table[i].name)) return table[i].value;
    return -1;
}

// ===========================================================================
// 2. control_binding_sets.xtbl
// ===========================================================================
BindingSetControl ParseBindingSetControl(const Node* row) {
    BindingSetControl c;
    c.action = getText(row, "Action");
    c.key1 = getText(row, "Key");
    c.key2 = getText(row, "Alt_Key");
    c.mouse1 = LookupNamedValue(FindChild(row, "Mouse_Button"), kMouseButtonTable);
    c.mouse2 = LookupNamedValue(FindChild(row, "Alt_Mouse_Button"), kMouseButtonTable);
    c.debugOnly = ReadBoolAlways(row, "Debug_Only");
    c.nonReleaseFinal = ReadBoolAlways(row, "Non_Release_Final");
    return c;
}

BindingSetAxis ParseBindingSetAxis(const Node* row) {
    BindingSetAxis a;
    a.action = getText(row, "Action");
    a.key1 = getText(row, "Key_Pos");
    a.key2 = getText(row, "Key_Neg");
    a.key3 = getText(row, "Alt_Key_Pos");
    a.key4 = getText(row, "Alt_Key_Neg");
    a.mouse1 = LookupNamedValue(FindChild(row, "Mouse_Button_Pos"), kMouseButtonTable);
    a.mouse2 = LookupNamedValue(FindChild(row, "Mouse_Button_Neg"), kMouseButtonTable);
    a.mouse3 = LookupNamedValue(FindChild(row, "Alt_Mouse_Button_Pos"), kMouseButtonTable);
    a.mouse4 = LookupNamedValue(FindChild(row, "Alt_Mouse_Button_Neg"), kMouseButtonTable);
    a.mouseAxis = LookupNamedValue(FindChild(row, "Mouse_Axis"), kMouseAxisTable);
    return a;
}

BindingSet ParseBindingSet(const Node* row) {
    BindingSet b;
    b.name = getText(row, "Name");
    b.commonBindings = GetBool(row, "Common_Bindings");
    const Node* buttonControls = FindChild(row, "Button_Controls");
    for (const Node* c : Children(buttonControls, "Control")) b.controls.push_back(ParseBindingSetControl(c));
    const Node* axisControls = FindChild(row, "Axis_Controls");
    for (const Node* a : Children(axisControls, "Axis")) b.axes.push_back(ParseBindingSetAxis(a));
    return b;
}

std::vector<BindingSet> ParseControlBindingSetsTable(const Document& doc) {
    std::vector<BindingSet> out;
    for (const Node* row : Children(doc.table(), "Binding_Set")) out.push_back(ParseBindingSet(row));
    return out;
}

// ===========================================================================
// 3. control_schemes.xtbl
// ===========================================================================
ControlSchemeControl ParseControlSchemeControl(const Node* row) {
    ControlSchemeControl c;
    c.action = getText(row, "Action");
    c.button = LookupNamedValue(FindChild(row, "Button"), kGamepadButtonTable);
    c.alternateButton = LookupNamedValue(FindChild(row, "Alternate_Button"), kGamepadButtonTable);
    c.stickDirection = LookupNamedValue(FindChild(row, "Stick_Direction"), kStickDirectionTable);
    c.actionName = getText(row, "Action_Name");
    c.x360 = GetBool(row, "X360");
    c.debugOnly = ReadBoolAlways(row, "Debug_Only");
    c.nonReleaseFinal = ReadBoolAlways(row, "Non_Release_Final");
    return c;
}

ControlSchemeAxis ParseControlSchemeAxis(const Node* row) {
    ControlSchemeAxis a;
    a.action = getText(row, "Action");
    a.axis = LookupNamedValue(FindChild(row, "Axis"), kStickAxisTable);
    a.inverted = ReadBoolAlways(row, "Inverted");
    a.alternateAxis = LookupNamedValue(FindChild(row, "Alternate_Axis"), kStickAxisTable);
    // CORRECTED 2026-10-01 (spec 3.2): the real loader reads "Inverted" a
    // SECOND time here, not "Alternate_Inverted" - the file's own
    // Alternate_Inverted values are dead data the loader never looks up.
    a.alternateInverted = ReadBoolAlways(row, "Inverted");
    return a;
}

ControlScheme ParseControlScheme(const Node* row) {
    ControlScheme s;
    s.name = getText(row, "Name");
    s.weaponSelectionStick = LookupNamedValue(FindChild(row, "Weapon_Selection_Stick"), kGamepadButtonTable);
    const Node* controlsWrap = FindChild(row, "Controls");
    for (const Node* c : Children(controlsWrap, "Control")) s.controls.push_back(ParseControlSchemeControl(c));
    const Node* axesWrap = FindChild(row, "Axes");
    for (const Node* a : Children(axesWrap, "Axis")) s.axes.push_back(ParseControlSchemeAxis(a));
    return s;
}

std::vector<ControlScheme> ParseControlSchemesTable(const Document& doc) {
    std::vector<ControlScheme> out;
    for (const Node* row : Children(doc.table(), "Control_Scheme")) out.push_back(ParseControlScheme(row));
    return out;
}

// ===========================================================================
// 6. qte.xtbl
// ===========================================================================
QteGlobalParams ParseQteGlobalParams(const Node* row) {
    QteGlobalParams q;
    q.hudTime = ReadFloatAlways(row, "Hud_Time");
    q.cooldownTime = ReadFloatAlways(row, "Cooldown_Time");
    q.respect = ReadInt32Always(row, "Respect");
    q.maxLifetimeRespect = ReadInt32Always(row, "Max_Lifetime_Respect");
    q.cash = ReadFloatAlways(row, "Cash");
    return q;
}

std::optional<QteGlobalParams> ParseQteTable(const Document& doc) {
    const Node* row = FindChild(doc.table(), "QTE");
    if (!row) return std::nullopt;
    return ParseQteGlobalParams(row);
}

// ===========================================================================
// 7. qte_sequences.xtbl
// ===========================================================================
QteNodeNpcAnimation ParseQteNodeNpcAnimation(const Node* row) {
    QteNodeNpcAnimation n;
    n.npcName = getText(row, "NPC_Name");
    n.personaLine = getText(row, "Persona_Line");
    n.lineDelay = GetInt32(row, "Line_Delay");
    n.enterActionAnim = getText(row, "Enter_Action");
    n.loopingStateAnim = getText(row, "Looping_State");
    n.additiveActionAnim = getText(row, "Additive_Action");
    return n;
}

QteNode ParseQteNode(const Node* row) {
    QteNode n;
    n.nodeName = getText(row, "Node_Name");
    n.hudInterface = getText(row, "HUD_Interface");
    n.failTime = GetInt32(row, "Fail_Time");

    const Node* persona = FindChild(row, "Persona");
    n.personaLine = getText(persona, "Persona_Line");
    n.personaLineDelay = ReadInt32Always(persona, "Line_Delay");
    const Node* partnerPersona = FindChild(row, "Partner_Persona");
    n.partnerPersonaLine = getText(partnerPersona, "Persona_Line");
    n.partnerPersonaLineDelay = ReadInt32Always(partnerPersona, "Line_Delay");

    n.cameraShake = getText(row, "Camera_Shake");
    n.healthChange = GetFloat(row, "Health_Change");
    n.playerImpactDmg = GetInt32(row, "Player_Impact_Dmg");

    const Node* enterAction = FindChild(row, "Enter_Action");
    const Node* enterSyncedMeleeMove = FindChild(enterAction, "Synced_Melee_Move");
    n.enterMeleeMove = getText(enterSyncedMeleeMove, "Melee_Move");
    n.enterMeleeMovePlayerIsAttacker = ReadBoolAlways(enterSyncedMeleeMove, "Player_Is_Attacker");
    n.enterPlayerAnimation = getText(enterAction, "Player_Animation");

    const Node* loopingState = FindChild(row, "Looping_State");
    const Node* loopingSyncedState = FindChild(loopingState, "Synced_State");
    n.loopingSyncedAnimation = getText(loopingSyncedState, "Synced_Animation");
    n.loopingSyncedPlayerIsAttacker = ReadBoolAlways(loopingSyncedState, "Player_Is_Attacker");
    n.loopingPlayerAnimationState = getText(loopingState, "Player_Animation_State");

    const Node* additiveAction = FindChild(row, "Additive_Action");
    const Node* additiveSyncedMeleeMove = FindChild(additiveAction, "Synced_Melee_Move");
    n.additiveMeleeMove = getText(additiveSyncedMeleeMove, "Melee_Move");
    n.additiveMeleeMovePlayerIsAttacker = ReadBoolAlways(additiveSyncedMeleeMove, "Player_Is_Attacker");
    n.additivePlayerAnimation = getText(additiveAction, "Player_Animation");
    n.additiveNewFailTime = GetUInt32(additiveAction, "New_Fail_Time");
    n.additiveActionCount = GetInt32(additiveAction, "Action_Count");

    const Node* shootingMode = FindChild(additiveAction, "Shooting_Mode");
    n.shootingModePresent = (shootingMode != nullptr);
    n.shootingModeRecoil = ReadFloatAlways(shootingMode, "Recoil");
    n.shootingModeSyncedPartnerFlinch = getText(shootingMode, "Synced_Partner_Flinch");
    const Node* restrictCam = FindChild(shootingMode, "Restrict_Camera_Angle");
    n.restrictCameraMinHeading = ReadFloatAlways(restrictCam, "Min_Heading");
    n.restrictCameraMaxHeading = ReadFloatAlways(restrictCam, "Max_Heading");
    n.restrictCameraMinPitch = ReadFloatAlways(restrictCam, "Min_Pitch");
    n.restrictCameraMaxPitch = ReadFloatAlways(restrictCam, "Max_Pitch");
    const Node* targetCam = FindChild(shootingMode, "Target_Camera_Angle");
    n.targetCameraMinHeadingDeg = ReadFloatAlways(targetCam, "Min_Heading");
    n.targetCameraMaxHeadingDeg = ReadFloatAlways(targetCam, "Max_Heading");
    n.targetCameraMinPitchDeg = ReadFloatAlways(targetCam, "Min_Pitch");
    n.targetCameraMaxPitchDeg = ReadFloatAlways(targetCam, "Max_Pitch");

    const Node* npcAnimationsWrap = FindChild(row, "NPC_Animations");
    for (const Node* a : Children(npcAnimationsWrap, "NPC_Animation")) n.npcAnimations.push_back(ParseQteNodeNpcAnimation(a));

    const Node* onSuccess = FindChild(row, "On_Success");
    n.onSuccessInterrupt = ReadBoolAlways(onSuccess, "Interrupt");
    n.onSuccessKeepHud = ReadBoolAlways(onSuccess, "Keep_HUD");
    n.onSuccessKillPartner = ReadBoolAlways(onSuccess, "Kill_Partner");
    n.onSuccessRepeat = ReadInt32Always(onSuccess, "Repeat");
    n.onSuccessGoToNode = getText(onSuccess, "Go_To_Node");

    const Node* onFail = FindChild(row, "On_Fail");
    n.onFailGoToNode = getText(onFail, "Go_To_Node");
    n.onFailForceSuccess = GetBool(onFail, "Force_Success");
    n.onFailKeepHud = ReadBoolAlways(onFail, "Keep_HUD");

    n.triggeredExplosion = getText(row, "Triggered_Explosion");
    return n;
}

QteSequence ParseQteSequence(const Node* row) {
    QteSequence s;
    s.name = getText(row, "Name");
    s.disablePlayer = ReadBoolAlways(row, "Disable_Player");
    s.viewRemotely = ReadBoolAlways(row, "View_Remotely");
    s.playerWeapon = getText(row, "Player_Weapon");
    const Node* succesState = FindChild(row, "Succes_State");  // sic: one 's' - see 7.1
    // CORRECTED 2026-10-01 (7.1/7.2): 0x0095DA50 is a hash-indexed name->u16
    // resolver (a DIFFERENT table from Looping_State's own Synced_Animation,
    // which goes through 0x004BF810) - so this element is a NAME, not a raw
    // number. Was previously read with GetUInt16 (a real bug); now raw text,
    // matching every other un-modelled resolver field in this struct.
    s.successAnimation = getText(succesState, "Synced_Animation");
    s.successPlayerIsAttacker = ReadBoolAlways(succesState, "Player_Is_Attacker");
    const Node* animatedNpcs = FindChild(row, "Animated_NPCs");
    for (const Node* npc : Children(animatedNpcs, "Animated_NPC"))
        if (auto t = getText(npc, "NPC_Name")) s.animatedNpcNames.push_back(*t);
    const Node* qteNodes = FindChild(row, "QTE_Nodes");
    for (const Node* n : Children(qteNodes, "QTE_Node")) s.nodes.push_back(ParseQteNode(n));
    return s;
}

std::vector<QteSequence> ParseQteSequencesTable(const Document& doc) {
    std::vector<QteSequence> out;
    for (const Node* row : Children(doc.table(), "QTE")) out.push_back(ParseQteSequence(row));
    return out;
}

// ===========================================================================
// 8.1 vehicle_cameras.xtbl / vehicle_group_cameras.xtbl
// ===========================================================================
VehicleCameraFields ParseVehicleCameraFields(const Node* row) {
    VehicleCameraFields f;
    f.cameraProximityLockY = GetFloat(row, "Camera_Proximity_Lock_Y");
    f.primaryCameraAnglePresent = FindChild(row, "Primary_Camera_Angle") != nullptr;
    f.secondaryCameraAnglePresent = FindChild(row, "Secondary_Camera_Angle") != nullptr;
    f.rcCameraAnglePresent = FindChild(row, "RC_Camera_Angle") != nullptr;

    const Node* positionBased = FindChild(row, "Position_Based");
    f.positionBasedPresent = positionBased != nullptr;
    f.positionSwingRateFwd = GetFloat(positionBased, "Swing_Rate_Fwd");
    f.positionSwingRateRev = GetFloat(positionBased, "Swing_Rate_Rev");
    f.positionFromStickyFwd = GetFloat(positionBased, "From_Sticky_Fwd");
    f.positionFromStickyRev = GetFloat(positionBased, "From_Sticky_Rev");
    f.positionHeadingRetentionNormal = GetFloat(positionBased, "Heading_Retention_Normal");
    f.positionHeadingRetentionTurning = GetFloat(positionBased, "Heading_Retention_Turning");

    const Node* sphereBased = FindChild(row, "Sphere_Based");
    f.sphereBasedPresent = sphereBased != nullptr;
    f.sphereSlerpValue = ReadFloatAlways(sphereBased, "Slerp_Value");
    f.sphereFromStickySlerpValue = GetFloat(sphereBased, "From_Sticky_Slerp_Value");
    f.sphereMinAngleDeg = GetFloat(sphereBased, "Min_Angle");

    const Node* cameraRoll = FindChild(row, "Camera_Roll");
    f.cameraRollTypeText = getText(cameraRoll, "Roll_Type");
    f.cameraRollIntensityMultiplier = GetFloat(cameraRoll, "Intensity_Multiplier");

    // CORRECTED 2026-10-01 (8.1): these are CHILD ELEMENTS, not row
    // attributes - the former "attribute vs child element" OPEN question is
    // now settled (getter 0x00DABA40 reads child elements everywhere else in
    // this group). The spec does not state the VALUE comparison's
    // case-sensitivity, so this reader compares case-insensitively (NameEquals),
    // the same policy as every other text-literal match here.
    if (auto skip = getText(row, "skip_camera_transition")) f.skipCameraTransition = NameEquals(*skip, "yes");
    if (auto alt = getText(row, "use_alt_freckle_cam")) f.useAltFreckleCam = NameEquals(*alt, "yes");

    f.cameraFovScale = GetFloat(row, "camera_fov_scale");
    f.maxFov = ReadFloatAlways(row, "Max_FOV");
    f.minFollowDistMultiplier = GetFloat(row, "Min_Follow_Dist_Multiplier");
    return f;
}

VehicleCameraRow ParseVehicleCameraRow(const Node* row) {
    VehicleCameraRow r;
    r.name = getText(row, "Name");
    r.camera = ParseVehicleCameraFields(row);
    return r;
}

std::vector<VehicleCameraRow> ParseVehicleCamerasTable(const Document& doc) {
    std::vector<VehicleCameraRow> out;
    for (const Node* row : Children(doc.table(), "Vehicle")) out.push_back(ParseVehicleCameraRow(row));
    return out;
}

std::vector<VehicleCameraRow> ParseVehicleGroupCamerasTable(const Document& doc) {
    std::vector<VehicleCameraRow> out;
    for (const Node* row : Children(doc.table(), "Vehicle_Group")) out.push_back(ParseVehicleCameraRow(row));
    return out;
}

// ===========================================================================
// 8.2 item_cust_cameras.xtbl / player_cust_cameras_new.xtbl /
// vehicle_cust_cameras_new.xtbl
// ===========================================================================
CustCameraView ParseCustCameraView(const Node* row) {
    CustCameraView v;
    v.name = getText(row, "Name");
    v.distance = ReadFloatAlways(row, "distance");
    v.height = ReadFloatAlways(row, "height");
    v.subjHeading = ReadFloatAlways(row, "subj_heading");
    v.camHeading = ReadFloatAlways(row, "cam_heading");
    v.pitch = ReadFloatAlways(row, "pitch");
    v.fieldOfView = ReadFloatAlways(row, "field_of_view");
    v.snap = ReadBoolAlways(row, "snap");
    v.standardDef = FindChild(row, "standard_def") != nullptr;
    return v;
}

CustCameraSet ParseCustCameraSet(const Node* row) {
    CustCameraSet s;
    s.name = getText(row, "Name");
    const Node* viewList = FindChild(row, "camera_view_list");
    for (const Node* v : Children(viewList, "camera_view")) s.views.push_back(ParseCustCameraView(v));
    return s;
}

std::vector<CustCameraSet> ParseCustCamerasTable(const Document& doc) {
    std::vector<CustCameraSet> out;
    for (const Node* row : Children(doc.table(), "camera")) out.push_back(ParseCustCameraSet(row));
    return out;
}

// ===========================================================================
// 9. hud_qte_interface_presets.xtbl + hud_qte_interface.xtbl
// ===========================================================================
HudQtePreset ParseHudQtePreset(const Node* row) {
    HudQtePreset p;
    p.name = getText(row, "Name");
    p.xPosition = ReadInt32Always(row, "X_Position");
    p.yPosition = ReadInt32Always(row, "Y_Position");
    p.xPositionSD = ReadInt32Always(row, "X_Position_SD");
    p.yPositionSD = ReadInt32Always(row, "Y_Position_SD");
    return p;
}

std::vector<HudQtePreset> ParseHudQteInterfacePresetsTable(const Document& doc) {
    std::vector<HudQtePreset> out;
    for (const Node* row : Children(doc.table(), "Hud_qte_preset")) out.push_back(ParseHudQtePreset(row));
    return out;
}

HudQteInterface ParseHudQteInterface(const Node* row) {
    HudQteInterface h;
    h.name = getText(row, "Name");
    h.buttonType = EnumIndex(FindChild(row, "Button_Type"), kQteButtonTypeNames.data(), kQteButtonTypeNames.size());
    // CORRECTED 2026-10-01 (9.2): raw text, not EnumIndex - see tables.h's
    // HudQteInterface::buttonAnimationType comment (real table has 9 slots,
    // only 3 names known; closing it would mis-map unknown-but-real values).
    h.buttonAnimationType = getText(row, "Button_Animation_Type");
    h.buttonAction = getText(row, "Button_Action");
    h.axisAction = getText(row, "Axis_Action");
    h.axisDirPos = ReadBoolAlways(row, "Axis_Dir_Pos");
    h.position = getText(row, "Position");
    h.positionPC = getText(row, "Position_PC");
    return h;
}

std::vector<HudQteInterface> ParseHudQteInterfaceTable(const Document& doc) {
    std::vector<HudQteInterface> out;
    for (const Node* row : Children(doc.table(), "Hud_qte")) out.push_back(ParseHudQteInterface(row));
    return out;
}

// ===========================================================================
// 10. user_interface.xtbl
// ===========================================================================
UiSlotOffset ParseUiSlotOffset(const Node* row) {
    UiSlotOffset s;
    s.resolutionRatio = getText(row, "ResolutionRatio");
    s.xOffset = ReadFloatAlways(row, "XOffset");
    s.yOffset = ReadFloatAlways(row, "YOffset");
    s.scale = ReadFloatAlways(row, "Scale");
    s.alpha = ReadFloatAlways(row, "Alpha");
    return s;
}

UiPart ParseUiPart(const Node* row) {
    UiPart p;
    p.name = getText(row, "Name");
    const Node* slotList = FindChild(row, "SlotList");
    for (const Node* s : Children(slotList, "SlotOffset")) p.slots.push_back(ParseUiSlotOffset(s));
    // CORRECTED 2026-10-01 (10): i32/f32, if-present (was ReadFloatAlways for
    // all four - both the type and the always/if-present reader were wrong
    // for XOffset/YOffset; only the always/if-present part was wrong for
    // Scale/Alpha).
    p.xOffset = GetInt32(row, "XOffset");
    p.yOffset = GetInt32(row, "YOffset");
    p.scale = GetFloat(row, "Scale");
    p.alpha = GetFloat(row, "Alpha");
    return p;
}

UiClusterOffset ParseUiClusterOffset(const Node* row) {
    UiClusterOffset c;
    c.resolutionRatio = getText(row, "ResolutionRatio");
    // CORRECTED 2026-10-01 (10): i32, if-present (was ReadFloatAlways).
    c.xOffset = GetInt32(row, "XOffset");
    c.yOffset = GetInt32(row, "YOffset");
    return c;
}

UiCluster ParseUiCluster(const Node* row) {
    UiCluster c;
    c.clusterName = getText(row, "ClusterName");
    c.xPosition = ReadFloatAlways(row, "XPosition");
    c.yPosition = ReadFloatAlways(row, "YPosition");
    const Node* offsetList = FindChild(row, "ClusterOffsetList");
    for (const Node* o : Children(offsetList, "ClusterOffset")) c.offsets.push_back(ParseUiClusterOffset(o));
    const Node* bitmapSlots = FindChild(row, "BitmapSlots");
    for (const Node* p : Children(bitmapSlots, "Part")) c.bitmapParts.push_back(ParseUiPart(p));
    return c;
}

UserInterfaceRow ParseUserInterfaceRow(const Node* row) {
    UserInterfaceRow r;
    r.name = getText(row, "Name");
    const Node* resolutionList = FindChild(row, "ResolutionList");
    const Node* resolutions = FindChild(resolutionList, "Resolutions");
    for (const Node* res : Children(resolutions, "Resolution"))
        if (res->text()) r.resolutions.push_back(*res->text());
    const Node* uiClusters = FindChild(row, "UIClusters");
    for (const Node* c : Children(uiClusters, "Cluster")) r.clusters.push_back(ParseUiCluster(c));
    return r;
}

std::vector<UserInterfaceRow> ParseUserInterfaceTable(const Document& doc) {
    std::vector<UserInterfaceRow> out;
    for (const Node* row : Children(doc.table(), "UserInterface")) out.push_back(ParseUserInterfaceRow(row));
    return out;
}

const UserInterfaceRow* FindActiveUserInterfaceRow(const std::vector<UserInterfaceRow>& rows) {
    for (const UserInterfaceRow& r : rows)
        if (r.name && NameEquals(*r.name, "XBox2")) return &r;
    return nullptr;
}

// ===========================================================================
// 11. control_scheme_text.xtbl
// ===========================================================================
ControlSchemeTextEntry ParseControlSchemeTextEntry(const Node* row) {
    ControlSchemeTextEntry e;
    e.control1 = getText(row, "Control1");
    e.locDesc = getText(row, "LocDesc");
    e.platform = getText(row, "Platform");
    return e;
}

// Platform filter + infinite-loop-hazard note: see tables.h's
// ControlSchemeText/ControlSchemeTextEntry struct banner (spec 11) for the
// full citation and precedent discussion. Short version: only a Control
// whose Platform text is exactly "360" or "All" (case-sensitive) is kept;
// FindChild()/Children() are both null-safe, so a Controls-less row yields
// an empty vector here rather than reproducing the real loader's re-entry
// hazard.
ControlSchemeText ParseControlSchemeText(const Node* row) {
    ControlSchemeText t;
    t.name = getText(row, "Name");
    const Node* controls = FindChild(row, "Controls");
    for (const Node* c : Children(controls, "Control")) {
        const std::optional<std::string> platform = getText(c, "Platform");
        const bool platformKept = platform.has_value() && (*platform == "360" || *platform == "All");
        if (!platformKept) continue;
        t.controls.push_back(ParseControlSchemeTextEntry(c));
    }
    return t;
}

std::vector<ControlSchemeText> ParseControlSchemeTextTable(const Document& doc) {
    std::vector<ControlSchemeText> out;
    for (const Node* row : Children(doc.table(), "ControlScheme")) out.push_back(ParseControlSchemeText(row));
    return out;
}

// ===========================================================================
// 12. voice_control.xtbl
// ===========================================================================
VoiceControlEntry ParseVoiceControlEntry(const Node* row) {
    VoiceControlEntry e;
    e.voicelineId = ReadInt32Always(row, "Voiceline_id");
    e.localCooldownMs = ReadInt32Always(row, "Local_cooldown");
    e.globalCooldownMs = ReadInt32Always(row, "Global_cooldown");
    e.minDelayRaw = ReadInt32Always(row, "Min_delay");
    e.maxDelayRaw = ReadInt32Always(row, "Max_delay");
    e.playPercent = ReadInt32Always(row, "Play_percent");
    e.priority = GetInt32(row, "Priority");
    // External_source/Play_event: "always" reader (CORRECTED 2026-10-01,
    // was GetUInt32/if-present) - see tables.h's VoiceControlEntry fields
    // for the full citation.
    e.externalSource = ReadUInt32Always(row, "External_source");
    e.playEvent = ReadUInt32Always(row, "Play_event");
    return e;
}

std::vector<VoiceControlEntry> ParseVoiceControlTable(const Document& doc) {
    std::vector<VoiceControlEntry> out;
    for (const Node* row : Children(doc.table(), "Entry")) out.push_back(ParseVoiceControlEntry(row));
    return out;
}

uint32_t VoiceControlEntry::LocalCooldownField() const {
    return static_cast<uint32_t>(clampT<int32_t>(localCooldownMs.value / 1000, 0, 63));
}
uint32_t VoiceControlEntry::GlobalCooldownField() const {
    return static_cast<uint32_t>(clampT<int32_t>(globalCooldownMs.value / 1000, 0, 15));
}
uint32_t VoiceControlEntry::MinDelayField() const {
    return static_cast<uint32_t>(clampT<int32_t>(minDelayRaw.value / 70, 0, 31));
}
uint32_t VoiceControlEntry::MaxDelayField() const {
    return static_cast<uint32_t>(clampT<int32_t>((maxDelayRaw.value - minDelayRaw.value) / 80, 0, 63));
}
uint32_t VoiceControlEntry::PlayPercentTier() const {
    const int32_t p = playPercent.value;
    if (p < 26) return 0;
    if (p < 51) return 1;
    if (p < 76) return 2;
    return 3;
}
uint32_t VoiceControlEntry::PriorityField() const {
    return static_cast<uint32_t>(clampT<int32_t>(PriorityOrDefault(), 0, 31)) & 0x1Fu;
}

// ===========================================================================
// 13. credits_pc.xtbl
// ===========================================================================
CreditsItem ParseCreditsItem(const Node* row) {
    CreditsItem it;
    it.name = getText(row, "name");
    it.desc = getText(row, "desc");
    it.typeOverride = EnumIndex(FindChild(row, "type_override"), kCreditsItemTypeNames.data(), kCreditsItemTypeNames.size());
    return it;
}

CreditsHeading ParseCreditsHeading(const Node* row) {
    CreditsHeading h;
    h.heading = getText(row, "heading");
    h.type = EnumIndex(FindChild(row, "type"), kCreditsItemTypeNames.data(), kCreditsItemTypeNames.size());
    const Node* itemsWrap = FindChild(row, "items");
    for (const Node* it : Children(itemsWrap, "items")) h.items.push_back(ParseCreditsItem(it));
    return h;
}

CreditsSection ParseCreditsSection(const Node* row) {
    CreditsSection s;
    s.name = getText(row, "Name");
    s.sectionTitle = getText(row, "section_title");
    const Node* headingsWrap = FindChild(row, "headings");
    for (const Node* h : Children(headingsWrap, "headings")) s.headings.push_back(ParseCreditsHeading(h));
    return s;
}

std::vector<CreditsSection> ParseCreditsTable(const Document& doc) {
    std::vector<CreditsSection> out;
    for (const Node* row : Children(doc.table(), "Credits")) out.push_back(ParseCreditsSection(row));
    return out;
}

// ===========================================================================
// 14. control_filters.xtbl + control_parameters.xtbl
// ===========================================================================
ControlFilter ParseControlFilter(const Node* row) {
    ControlFilter f;
    f.validated = GetBool(row, "validated");
    f.name = getText(row, "name");
    f.type = getText(row, "type");
    f.sourceName = getText(row, "source_name");
    f.inputMin = GetFloat(row, "input_min");
    f.inputMax = GetFloat(row, "input_max");
    f.outputMin = GetFloat(row, "output_min");
    f.outputMax = GetFloat(row, "output_max");
    f.multiplier = GetFloat(row, "multiplier");
    return f;
}

std::vector<ControlFilter> ParseControlFiltersTable(const Document& doc) {
    std::vector<ControlFilter> out;
    const Node* wrap = FindChild(doc.root(), "control_filters");
    for (const Node* row : Children(wrap, "control_filter")) out.push_back(ParseControlFilter(row));
    return out;
}

ControlParameter ParseControlParameter(const Node* row) {
    ControlParameter p;
    p.name = getText(row, "name");
    p.dataType = getText(row, "data_type");
    p.defaultValue = getText(row, "default_value");
    p.minValue = GetFloat(row, "min_value");
    p.maxValue = GetFloat(row, "max_value");
    return p;
}

std::vector<ControlParameter> ParseControlParametersTable(const Document& doc) {
    std::vector<ControlParameter> out;
    const Node* wrap = FindChild(doc.root(), "control_parameters");
    for (const Node* row : Children(wrap, "control_parameter")) out.push_back(ParseControlParameter(row));
    return out;
}

}  // namespace sr3tables_ui_controls
