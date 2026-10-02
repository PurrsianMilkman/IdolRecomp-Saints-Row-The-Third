// Parse functions for sr3tables_diversions (see include/sr3tables_diversions/
// tables.h and horde_mode.h for the struct definitions and per-field spec
// citations). Built ONLY on sr3xtbl's accessors (include/sr3xtbl/xtbl.h);
// nothing here touches the game executable, disassembly or decompiled code.
// Source: spec-tables-diversions.md.

#include "sr3tables_diversions/tables.h"

#include <array>
#include <string>

namespace sr3tables_diversions {

namespace {

using sr3xtbl::Always;
using sr3xtbl::Node;

std::optional<std::string> getText(const Node* node, std::string_view name = {}) {
    const std::string* t = sr3xtbl::ChildText(node, name);
    if (!t) return std::nullopt;
    return *t;
}

// ---------------------------------------------------------------------------
// Shared shapes (spec 1.2 items 2-3; see tables.h for the full rationale).
// ---------------------------------------------------------------------------
RewardTriad ParseRewardTriad(const Node* row) {
    RewardTriad r;
    r.maxRespect = sr3xtbl::ReadInt32Always(row, "Max_Respect");
    r.maxLifetimeRespect = sr3xtbl::ReadInt32Always(row, "Max_Lifetime_Respect");
    r.maxCash = sr3xtbl::ReadFloatAlways(row, "Max_Cash");
    return r;
}

RecordNotificationTriad ParseRecordTriad(const Node* row) {
    RecordNotificationTriad t;
    t.displayTimeSeconds = sr3xtbl::GetFloat(row, "Record_Display_Time");
    t.queueTimeSeconds = sr3xtbl::GetFloat(row, "Record_Queue_Time");
    t.thresholdPercent = sr3xtbl::ReadFloatAlways(row, "Record_Threshold");
    return t;
}

}  // namespace

// ===========================================================================
// 2.2 stunt_drifting.xtbl - row Stunt_Drifting
// ===========================================================================
StuntDrifting ParseStuntDrifting(const Node* row) {
    StuntDrifting d;
    d.minTimeSeconds = sr3xtbl::ReadFloatAlways(row, "Min_Time");
    d.maxTimeSeconds = sr3xtbl::ReadFloatAlways(row, "Max_Time");
    d.minSpeedMph = sr3xtbl::ReadFloatAlways(row, "Min_Speed");
    d.maxTimeNotDriftingSeconds = sr3xtbl::ReadFloatAlways(row, "Max_Time_Not_Drifting");
    d.minDriftAngleDegrees = sr3xtbl::ReadFloatAlways(row, "Min_Drift_Angle");
    d.record = ParseRecordTriad(row);
    d.reward = ParseRewardTriad(row);
    return d;
}
std::optional<StuntDrifting> ParseStuntDriftingTable(const Document& doc) {
    const Node* row = sr3xtbl::FindChild(doc.table(), "Stunt_Drifting");
    if (!row) return std::nullopt;
    return ParseStuntDrifting(row);
}

// ===========================================================================
// 2.3 stunt_hijacking.xtbl - row Stunt_Hijacking
// ===========================================================================
StuntHijacking ParseStuntHijacking(const Node* row) {
    StuntHijacking h;
    h.hudTimeSeconds = sr3xtbl::ReadFloatAlways(row, "Hud_Time");
    h.cooldownTimeSeconds = sr3xtbl::ReadFloatAlways(row, "Cooldown_Time");
    h.onlyRewardHijacks = sr3xtbl::ReadBoolAlways(row, "Only_Reward_Hijacks");
    h.reward = ParseRewardTriad(row);
    return h;
}
std::optional<StuntHijacking> ParseStuntHijackingTable(const Document& doc) {
    const Node* row = sr3xtbl::FindChild(doc.table(), "Stunt_Hijacking");
    if (!row) return std::nullopt;
    return ParseStuntHijacking(row);
}

// ===========================================================================
// 2.4 stunt_near_miss.xtbl - row Near_Miss
// ===========================================================================
StuntNearMiss ParseStuntNearMiss(const Node* row) {
    StuntNearMiss n;
    n.minVehicles = sr3xtbl::ReadInt32Always(row, "Min_Vehicles");
    n.maxVehicles = sr3xtbl::ReadInt32Always(row, "Max_Vehicles");
    n.minSpeedMph = sr3xtbl::ReadFloatAlways(row, "Min_Speed");
    n.relativeSpeedMph = sr3xtbl::ReadFloatAlways(row, "Relative_Speed");
    n.vehicleDistance = sr3xtbl::ReadFloatAlways(row, "Vehicle_Distance");
    n.maxTimeBetweenMissesSeconds = sr3xtbl::ReadFloatAlways(row, "Max_Time_Between_Misses");
    n.minTimeBetweenMissesSeconds = sr3xtbl::ReadFloatAlways(row, "Min_Time_Between_Misses");
    n.delayAfterCrashSeconds = sr3xtbl::ReadFloatAlways(row, "Delay_After_Crash");
    n.delayAfterCompletionSeconds = sr3xtbl::ReadFloatAlways(row, "Delay_After_Completion");
    n.record = ParseRecordTriad(row);
    n.reward = ParseRewardTriad(row);
    return n;
}
std::optional<StuntNearMiss> ParseStuntNearMissTable(const Document& doc) {
    const Node* row = sr3xtbl::FindChild(doc.table(), "Near_Miss");
    if (!row) return std::nullopt;
    return ParseStuntNearMiss(row);
}

// ===========================================================================
// 2.5 stunt_back_seat_driver.xtbl - row Back_Seat_Driver
// ===========================================================================
StuntBackSeatDriver ParseStuntBackSeatDriver(const Node* row) {
    StuntBackSeatDriver b;
    b.maxDistance = sr3xtbl::ReadFloatAlways(row, "Max_Distance");
    b.minDistance = sr3xtbl::ReadFloatAlways(row, "Min_Distance");
    b.minSpeedMph = sr3xtbl::ReadFloatAlways(row, "Min_Speed");
    b.endTimeSeconds = sr3xtbl::ReadFloatAlways(row, "End_Time");
    b.record = ParseRecordTriad(row);
    b.reward = ParseRewardTriad(row);
    return b;
}
std::optional<StuntBackSeatDriver> ParseStuntBackSeatDriverTable(const Document& doc) {
    const Node* row = sr3xtbl::FindChild(doc.table(), "Back_Seat_Driver");
    if (!row) return std::nullopt;
    return ParseStuntBackSeatDriver(row);
}

// ===========================================================================
// 2.6 stunt_oncoming.xtbl - row Stunt_Oncoming
// ===========================================================================
StuntOncoming ParseStuntOncoming(const Node* row) {
    StuntOncoming o;
    o.maxDistance = sr3xtbl::ReadFloatAlways(row, "Max_Distance");
    o.minDistance = sr3xtbl::ReadFloatAlways(row, "Min_Distance");
    o.minSpeedMph = sr3xtbl::ReadFloatAlways(row, "Min_Speed");
    o.maxDamage = sr3xtbl::ReadInt32Always(row, "Max_Damage");
    o.angleToleranceDegrees = sr3xtbl::ReadFloatAlways(row, "Angle_Tolerance");
    o.laneTolerance = sr3xtbl::ReadFloatAlways(row, "Lane_Tolerance");
    o.maxTimeNotOncomingSeconds = sr3xtbl::ReadFloatAlways(row, "Max_Time_Not_Oncoming");
    o.record = ParseRecordTriad(row);
    o.reward = ParseRewardTriad(row);
    return o;
}
std::optional<StuntOncoming> ParseStuntOncomingTable(const Document& doc) {
    const Node* row = sr3xtbl::FindChild(doc.table(), "Stunt_Oncoming");
    if (!row) return std::nullopt;
    return ParseStuntOncoming(row);
}

// ===========================================================================
// 2.7 stunt_peel_out.xtbl - row Peel_Out
// ===========================================================================
StuntPeelOut ParseStuntPeelOut(const Node* row) {
    StuntPeelOut p;
    p.maxSpectators = sr3xtbl::ReadInt32Always(row, "Max_Spectators");
    p.minSpectators = sr3xtbl::ReadInt32Always(row, "Min_Spectators");
    p.maxWatchDistance = sr3xtbl::ReadFloatAlways(row, "Max_Watch_Distance");
    p.instanceDelaySeconds = sr3xtbl::ReadFloatAlways(row, "Instance_Delay");
    p.recordDisplayTimeSeconds = sr3xtbl::GetFloat(row, "Record_Display_Time");
    p.recordQueueTimeSeconds = sr3xtbl::GetFloat(row, "Record_Queue_Time");
    // spec 1.5 engine quirk: this field is authored identically to every
    // sibling table's Record_Threshold, but is converted differently at load
    // (see the field's own comment in tables.h). The raw read is identical.
    p.recordThresholdRaw = sr3xtbl::ReadFloatAlways(row, "Record_Threshold");
    p.reward = ParseRewardTriad(row);
    return p;
}
std::optional<StuntPeelOut> ParseStuntPeelOutTable(const Document& doc) {
    const Node* row = sr3xtbl::FindChild(doc.table(), "Peel_Out");
    if (!row) return std::nullopt;
    return ParseStuntPeelOut(row);
}

// ===========================================================================
// 2.8 stunt_wheels.xtbl - row Stunt_Wheels
// ===========================================================================
StuntWheelsSubStunt ParseStuntWheelsSubStunt(const Node* block) {
    StuntWheelsSubStunt s;
    s.maxDistance = sr3xtbl::ReadFloatAlways(block, "Max_Distance");
    s.minDistance = sr3xtbl::ReadFloatAlways(block, "Min_Distance");
    s.stuntToleranceSeconds = sr3xtbl::ReadFloatAlways(block, "Stunt_Tolerance");
    s.endTimeSeconds = sr3xtbl::ReadFloatAlways(block, "End_Time");
    s.reward = ParseRewardTriad(block);
    return s;
}
StuntWheels ParseStuntWheels(const Node* row) {
    StuntWheels w;
    // See tables.h's NOTE on StuntWheels: spec 2.8 CORRECTED 2026-10-02 -
    // +0x00-0x13 is never touched by FUN_006C16A0, so only the record triad
    // is a real top-level field; the old "7/7 top-level" framing does not
    // hold. The record triad (the only real top-level field) is modelled
    // here.
    w.record = ParseRecordTriad(row);
    w.twoWheels = ParseStuntWheelsSubStunt(sr3xtbl::FindChild(row, "Two_Wheels"));
    w.wheelie = ParseStuntWheelsSubStunt(sr3xtbl::FindChild(row, "Wheelie"));
    w.stoppie = ParseStuntWheelsSubStunt(sr3xtbl::FindChild(row, "Stoppie"));
    return w;
}
std::optional<StuntWheels> ParseStuntWheelsTable(const Document& doc) {
    const Node* row = sr3xtbl::FindChild(doc.table(), "Stunt_Wheels");
    if (!row) return std::nullopt;
    return ParseStuntWheels(row);
}

// ===========================================================================
// 2.9 stunt_jumping_diversion.xtbl - row Stunt_Jumping
// ===========================================================================
StuntJumpingAxis ParseStuntJumpingAxis(const Node* block) {
    StuntJumpingAxis a;
    a.maxValue = sr3xtbl::ReadFloatAlways(block, "Max_Value");
    a.minValue = sr3xtbl::ReadFloatAlways(block, "Min_Value");
    a.maxPercent = sr3xtbl::GetFloat(block, "Max_Percent");
    return a;
}
StuntJumpingVehicleType ParseStuntJumpingVehicleType(const Node* row) {
    StuntJumpingVehicleType v;
    // "Vehicle_Name", not "Name" - corrected against real shipped game DATA
    // (da_tables.vpp_pc), see the field's comment in tables.h.
    v.name = getText(row, "Vehicle_Name");
    v.landDistance = ParseStuntJumpingAxis(sr3xtbl::FindChild(row, "Land_Distance"));
    v.airHeight = ParseStuntJumpingAxis(sr3xtbl::FindChild(row, "Air_Height"));
    v.fallDist = ParseStuntJumpingAxis(sr3xtbl::FindChild(row, "Fall_Dist"));
    v.pitchRotation = ParseStuntJumpingAxis(sr3xtbl::FindChild(row, "Pitch_Rotation"));
    v.rollRotation = ParseStuntJumpingAxis(sr3xtbl::FindChild(row, "Roll_Rotation"));
    v.spinRotation = ParseStuntJumpingAxis(sr3xtbl::FindChild(row, "Spin_Rotation"));
    return v;
}
StuntJumpingDiversion ParseStuntJumpingDiversion(const Node* row) {
    StuntJumpingDiversion j;
    j.endTimeSeconds = sr3xtbl::ReadFloatAlways(row, "End_Time");
    j.record = ParseRecordTriad(row);
    j.reward = ParseRewardTriad(row);
    const Node* vehicleTypes = sr3xtbl::FindChild(row, "Vehicle_Types");
    for (const Node* vt : sr3xtbl::Children(vehicleTypes, "Vehicle_Type"))
        j.vehicleTypes.push_back(ParseStuntJumpingVehicleType(vt));
    return j;
}
std::optional<StuntJumpingDiversion> ParseStuntJumpingDiversionTable(const Document& doc) {
    const Node* row = sr3xtbl::FindChild(doc.table(), "Stunt_Jumping");
    if (!row) return std::nullopt;
    return ParseStuntJumpingDiversion(row);
}

// ===========================================================================
// 2.10 stunt_low_flying.xtbl - row Low_Flying
// ===========================================================================
StuntLowFlying ParseStuntLowFlying(const Node* row) {
    StuntLowFlying l;
    l.maxDistance = sr3xtbl::ReadFloatAlways(row, "Max_Distance");
    l.minDistance = sr3xtbl::ReadFloatAlways(row, "Min_Distance");
    l.verticalityMult = sr3xtbl::ReadFloatAlways(row, "Verticality_Mult");
    l.maxHeight = sr3xtbl::GetFloat(row, "Max_Height");
    l.activationHeight = sr3xtbl::GetFloat(row, "Activation_Height");
    l.minSpeedMph = sr3xtbl::ReadFloatAlways(row, "Min_Speed");
    l.maxTimeNotLowFlyingSeconds = sr3xtbl::ReadFloatAlways(row, "Max_Time_Not_Low_Flying");
    l.completionCooldownSeconds = sr3xtbl::ReadFloatAlways(row, "Completion_Cooldown");
    l.record = ParseRecordTriad(row);
    l.reward = ParseRewardTriad(row);
    return l;
}
std::optional<StuntLowFlying> ParseStuntLowFlyingTable(const Document& doc) {
    const Node* row = sr3xtbl::FindChild(doc.table(), "Low_Flying");
    if (!row) return std::nullopt;
    return ParseStuntLowFlying(row);
}

// ===========================================================================
// 3. barnstorming.xtbl - row Barnstorming
// ===========================================================================
Barnstorming ParseBarnstorming(const Node* row) {
    Barnstorming b;
    b.maxDistance = sr3xtbl::GetFloat(row, "Max_Distance");
    b.minSpeedMph = sr3xtbl::ReadFloatAlways(row, "Min_Speed");
    b.maxDamage = sr3xtbl::ReadInt32Always(row, "Max_Damage");
    b.crashDelaySeconds = sr3xtbl::ReadFloatAlways(row, "Crash_Delay");
    b.restartDelaySeconds = sr3xtbl::ReadFloatAlways(row, "Restart_Delay");
    b.knifeEdgeToleranceDegrees = sr3xtbl::ReadFloatAlways(row, "Knife_Edge_Tolerance");
    b.knifeEdgeMultiplier = sr3xtbl::ReadFloatAlways(row, "Knife_Edge_Multiplier");
    b.invertedToleranceDegrees = sr3xtbl::ReadFloatAlways(row, "Inverted_Tolerance");
    b.invertedMultiplier = sr3xtbl::ReadFloatAlways(row, "Inverted_Multiplier");
    b.allowReplay = sr3xtbl::ReadBoolAlways(row, "Allow_Replay");
    b.maxRespect = sr3xtbl::ReadInt32Always(row, "Max_Respect");
    b.maxCash = sr3xtbl::ReadFloatAlways(row, "Max_Cash");
    return b;
}
std::optional<Barnstorming> ParseBarnstormingTable(const Document& doc) {
    const Node* row = sr3xtbl::FindChild(doc.table(), "Barnstorming");
    if (!row) return std::nullopt;
    return ParseBarnstorming(row);
}

// ===========================================================================
// 4. base_jumping.xtbl - row Base_Jumping, 4 sub-blocks
// ===========================================================================
BaseJumpingGenInfo ParseBaseJumpingGenInfo(const Node* block) {
    BaseJumpingGenInfo g;
    g.startTimeSeconds = sr3xtbl::ReadFloatAlways(block, "Start_Time");
    g.completeDisplayTimeSeconds = sr3xtbl::ReadFloatAlways(block, "Complete_Display_Time");
    g.minAltitude = sr3xtbl::ReadFloatAlways(block, "Min_Altitude");
    g.maxAltitude = sr3xtbl::ReadFloatAlways(block, "Max_Altitude");
    g.yTolerance = sr3xtbl::ReadFloatAlways(block, "Y_Tolerance");
    g.record = ParseRecordTriad(block);
    return g;
}
BaseJumpingTargetLocationIndicator ParseBaseJumpingIndicator(const Node* block) {
    BaseJumpingTargetLocationIndicator ind;
    ind.indicatorType = getText(block, "Indicator_Type");
    const Node* flags = sr3xtbl::FindChild(block, "Indicator_Flags");
    ind.indicatorFlags =
        sr3xtbl::FlagMask(flags, kBaseJumpingIndicatorFlagNames.data(), kBaseJumpingIndicatorFlagNames.size());
    return ind;
}
BaseJumpingSecondaryEffects ParseBaseJumpingSecondaryEffects(const Node* block) {
    BaseJumpingSecondaryEffects e;
    e.effectName = getText(block, "Effect_Name");
    e.numEffects = sr3xtbl::GetInt32(block, "Num_Effects");
    e.effectsDist = sr3xtbl::GetFloat(block, "Effects_Dist");
    return e;
}
BaseJumpingTargetInfo ParseBaseJumpingTargetInfo(const Node* block) {
    BaseJumpingTargetInfo t;
    t.minTargetDistance = sr3xtbl::ReadFloatAlways(block, "Min_Target_Distance");
    t.minTargetCheckRadius = sr3xtbl::ReadFloatAlways(block, "Min_Target_Check_Radius");
    t.maxTargetDistance = sr3xtbl::GetFloat(block, "Max_Target_Distance");
    t.minTargetPercent = sr3xtbl::ReadFloatAlways(block, "Min_Target_Percent");
    t.maxTargetPercent = sr3xtbl::ReadFloatAlways(block, "Max_Target_Percent");
    t.disableHighwaySpawn = sr3xtbl::ReadBoolAlways(block, "Disable_Highway_Spawn");
    const Node* indicator = sr3xtbl::FindChild(block, "Target_Location_Indicator");
    t.targetLocationIndicatorPresent = indicator != nullptr;
    t.targetLocationIndicator = ParseBaseJumpingIndicator(indicator);
    t.primaryEffect = getText(block, "Primary_Effect");
    const Node* secondary = sr3xtbl::FindChild(block, "Secondary_Effects");
    t.secondaryEffectsPresent = secondary != nullptr;
    t.secondaryEffects = ParseBaseJumpingSecondaryEffects(secondary);
    return t;
}
BaseJumpingVehInfo ParseBaseJumpingVehInfo(const Node* block) {
    BaseJumpingVehInfo v;
    v.minVehDistance = sr3xtbl::ReadFloatAlways(block, "Min_Veh_Distance");
    v.maxVehDistance = sr3xtbl::ReadFloatAlways(block, "Max_Veh_Distance");
    return v;
}
BaseJumpingRewardTier ParseBaseJumpingRewardTier(const Node* row) {
    BaseJumpingRewardTier t;
    t.maxDistance = sr3xtbl::ReadFloatAlways(row, "Max_Distance");
    t.respect = sr3xtbl::ReadInt32Always(row, "Respect");
    t.cash = sr3xtbl::ReadFloatAlways(row, "Cash");
    return t;
}
BaseJumpingRewardInfo ParseBaseJumpingRewardInfo(const Node* block) {
    BaseJumpingRewardInfo r;
    r.maxLifetimeRespect = sr3xtbl::ReadInt32Always(block, "Max_Lifetime_Respect");
    // Reward_Tiers/Reward_Tier: container/row names both confirmed by spec
    // 4/14 prose ("Reward_Tiers ... Grid of Reward_Tier elements").
    const Node* tiers = sr3xtbl::FindChild(block, "Reward_Tiers");
    for (const Node* t : sr3xtbl::Children(tiers, "Reward_Tier"))
        r.rewardTiers.push_back(ParseBaseJumpingRewardTier(t));
    return r;
}
BaseJumping ParseBaseJumping(const Node* row) {
    BaseJumping bj;
    const Node* gen = sr3xtbl::FindChild(row, "Gen_Info");
    bj.genInfoPresent = gen != nullptr;
    bj.genInfo = ParseBaseJumpingGenInfo(gen);
    const Node* target = sr3xtbl::FindChild(row, "Target_Info");
    bj.targetInfoPresent = target != nullptr;
    bj.targetInfo = ParseBaseJumpingTargetInfo(target);
    const Node* veh = sr3xtbl::FindChild(row, "Veh_Info");
    bj.vehInfoPresent = veh != nullptr;
    bj.vehInfo = ParseBaseJumpingVehInfo(veh);
    const Node* reward = sr3xtbl::FindChild(row, "Reward_Info");
    bj.rewardInfoPresent = reward != nullptr;
    bj.rewardInfo = ParseBaseJumpingRewardInfo(reward);
    return bj;
}
std::optional<BaseJumping> ParseBaseJumpingTable(const Document& doc) {
    const Node* row = sr3xtbl::FindChild(doc.table(), "Base_Jumping");
    if (!row) return std::nullopt;
    return ParseBaseJumping(row);
}

// ===========================================================================
// 5. cat_and_mouse.xtbl - row Cat_And_Mouse
// ===========================================================================
CatAndMouseVehicleMatchup ParseCatAndMouseVehicleMatchup(const Node* row) {
    CatAndMouseVehicleMatchup m;
    m.catVehicle = getText(row, "Cat_Vehicle");
    m.catVehicleHealth = sr3xtbl::ReadInt32Always(row, "Cat_Vehicle_Health");
    m.mouseVehicle = getText(row, "Mouse_Vehicle");
    m.mouseVehicleHealth = sr3xtbl::ReadInt32Always(row, "Mouse_Vehicle_Health");
    m.mouseMinSpeedMph = sr3xtbl::ReadFloatAlways(row, "Mouse_Min_Speed");
    return m;
}
CatAndMouse ParseCatAndMouse(const Node* row) {
    CatAndMouse c;
    const Node* matchups = sr3xtbl::FindChild(row, "Vehicle_Matchups");
    for (const Node* m : sr3xtbl::Children(matchups, "Vehicle_Matchup"))
        c.vehicleMatchups.push_back(ParseCatAndMouseVehicleMatchup(m));
    c.triggerHoldStartDelaySeconds = sr3xtbl::ReadFloatAlways(row, "Trigger_Hold_Start_Delay");
    c.roundTimeSeconds = sr3xtbl::ReadFloatAlways(row, "Round_Time");
    c.roundsPerMatch = sr3xtbl::ReadInt32Always(row, "Rounds_Per_Match");
    c.cashReward = sr3xtbl::ReadFloatAlways(row, "Cash_Reward");
    c.mouseScoreIntervalSeconds = sr3xtbl::ReadFloatAlways(row, "Mouse_Score_Interval");
    c.mouseIntervalPoints = sr3xtbl::ReadInt32Always(row, "Mouse_Interval_Points");
    c.mouseNavpointPoints = sr3xtbl::ReadInt32Always(row, "Mouse_Navpoint_Points");
    c.mouseNavpointTimeAddedMs = sr3xtbl::ReadUInt32Always(row, "Mouse_Navpoint_Time_Added_ms");
    c.mouseNavpointStartDistance = sr3xtbl::ReadFloatAlways(row, "Mouse_Navpoint_Start_Distance");
    c.mouseNavpointDistanceIncrement = sr3xtbl::ReadFloatAlways(row, "Mouse_Navpoint_Distance_Increment");
    c.catSpawnDist = sr3xtbl::ReadFloatAlways(row, "Cat_Spawn_Dist");
    return c;
}
std::optional<CatAndMouse> ParseCatAndMouseTable(const Document& doc) {
    const Node* row = sr3xtbl::FindChild(doc.table(), "Cat_And_Mouse");
    if (!row) return std::nullopt;
    return ParseCatAndMouse(row);
}

// ===========================================================================
// 6.1 exploration_diversion.xtbl - row "Exploration" (NOT Exploration_Diversion)
// ===========================================================================
ExplorationDiversion ParseExplorationDiversion(const Node* row) {
    ExplorationDiversion e;
    e.maxExplorationDist = sr3xtbl::ReadFloatAlways(row, "Max_Exploration_Dist");
    e.messageTimeSeconds = sr3xtbl::ReadFloatAlways(row, "Message_Time");
    e.districtRespect = sr3xtbl::ReadInt32Always(row, "District_Respect");
    e.hoodRespect = sr3xtbl::ReadInt32Always(row, "Hood_Respect");
    e.secretLocationRespect = sr3xtbl::ReadInt32Always(row, "Secret_Location_Respect");
    e.stuntJumpRespect = sr3xtbl::ReadInt32Always(row, "Stunt_Jump_Respect");
    e.secretLocationCash = sr3xtbl::ReadFloatAlways(row, "Secret_Location_Cash");
    e.stuntJumpCash = sr3xtbl::ReadFloatAlways(row, "Stunt_Jump_Cash");
    return e;
}
std::optional<ExplorationDiversion> ParseExplorationDiversionTable(const Document& doc) {
    const Node* row = sr3xtbl::FindChild(doc.table(), "Exploration");
    if (!row) return std::nullopt;
    return ParseExplorationDiversion(row);
}

// ===========================================================================
// 6.2 mugging_diversion.xtbl - row Mugging_Diversion
// ===========================================================================
MuggingDiversion ParseMuggingDiversion(const Node* row) {
    MuggingDiversion m;
    m.muggingStartTimeSeconds = sr3xtbl::ReadFloatAlways(row, "Mugging_Start_Time");
    m.muggingCompleteTimeSeconds = sr3xtbl::ReadFloatAlways(row, "Mugging_Complete_Time");
    m.barterLineSeconds = sr3xtbl::ReadFloatAlways(row, "Barter_Line");
    m.cowerLineSeconds = sr3xtbl::ReadFloatAlways(row, "Cower_Line");
    m.maxDistance = sr3xtbl::ReadFloatAlways(row, "Max_Distance");
    m.defaultMinCash = sr3xtbl::ReadFloatAlways(row, "Default_Min_Cash");
    m.defaultMaxCash = sr3xtbl::ReadFloatAlways(row, "Default_Max_Cash");
    return m;
}
std::optional<MuggingDiversion> ParseMuggingDiversionTable(const Document& doc) {
    const Node* row = sr3xtbl::FindChild(doc.table(), "Mugging_Diversion");
    if (!row) return std::nullopt;
    return ParseMuggingDiversion(row);
}

// ===========================================================================
// 6.3 streaking.xtbl - row Streaking
// ===========================================================================
StreakingLevel ParseStreakingLevel(const Node* row) {
    StreakingLevel l;
    l.shockQuota = sr3xtbl::ReadInt32Always(row, "Shock_Quota");
    l.timeLimitSeconds = sr3xtbl::ReadFloatAlways(row, "Time_Limit");
    l.respect = sr3xtbl::ReadInt32Always(row, "Respect");
    l.cash = sr3xtbl::ReadFloatAlways(row, "Cash");
    return l;
}
Streaking ParseStreaking(const Node* row) {
    Streaking s;
    s.maxLifetimeRespect = sr3xtbl::ReadInt32Always(row, "Max_Lifetime_Respect");
    // Levels/Level: spec 6.3 names the grid "Levels" but does not spell out
    // its row element name; "Level" is used here by the same Grid-of-X
    // convention confirmed exactly for horde_mode.xtbl's own "Levels/Level"
    // grid (spec 13 cross-reference table) - not independently confirmed
    // for THIS table.
    const Node* levels = sr3xtbl::FindChild(row, "Levels");
    for (const Node* l : sr3xtbl::Children(levels, "Level"))
        s.levels.push_back(ParseStreakingLevel(l));
    return s;
}
std::optional<Streaking> ParseStreakingTable(const Document& doc) {
    const Node* row = sr3xtbl::FindChild(doc.table(), "Streaking");
    if (!row) return std::nullopt;
    return ParseStreaking(row);
}

// ===========================================================================
// 7. photo_op.xtbl - row Photo_Op
// ===========================================================================
PhotoOpAudience ParsePhotoOpAudience(const Node* block) {
    PhotoOpAudience a;
    const Node* presets = sr3xtbl::FindChild(block, "Char_Presets");
    for (const Node* p : sr3xtbl::Children(presets, "Char_Preset")) {
        if (auto n = getText(p, "Preset_Name")) a.charPresetNames.push_back(*n);
    }
    const Node* situations = sr3xtbl::FindChild(block, "Line_Situations");
    for (const Node* s : sr3xtbl::Children(situations, "Line_Situation")) {
        if (auto n = getText(s, "Line_Situation_Name")) a.lineSituationNames.push_back(*n);
    }
    return a;
}
PhotoOp ParsePhotoOp(const Node* row) {
    PhotoOp p;
    p.saintsLoved = ParsePhotoOpAudience(sr3xtbl::FindChild(row, "Saints_Loved"));
    p.saintsHated = ParsePhotoOpAudience(sr3xtbl::FindChild(row, "Saints_Hated"));
    p.triggerEffect = getText(row, "Trigger_Effect");
    return p;
}
std::optional<PhotoOp> ParsePhotoOpTable(const Document& doc) {
    const Node* row = sr3xtbl::FindChild(doc.table(), "Photo_Op");
    if (!row) return std::nullopt;
    return ParsePhotoOp(row);
}

// ===========================================================================
// 11. escort_name_generator.xtbl - row Name_Generator
// ===========================================================================
NaughtyName ParseNaughtyName(const Node* row) {
    NaughtyName n;
    n.firstName = getText(row, "First_name");
    n.secondName = getText(row, "Second_name");
    return n;
}
EscortNameGenerator ParseEscortNameGenerator(const Node* row) {
    EscortNameGenerator g;
    const Node* names = sr3xtbl::FindChild(row, "Naughty_names");
    for (const Node* n : sr3xtbl::Children(names, "Naughty_name"))
        g.naughtyNames.push_back(ParseNaughtyName(n));
    return g;
}
std::optional<EscortNameGenerator> ParseEscortNameGeneratorTable(const Document& doc) {
    const Node* row = sr3xtbl::FindChild(doc.table(), "Name_Generator");
    if (!row) return std::nullopt;
    return ParseEscortNameGenerator(row);
}

// ===========================================================================
// 12. shop_names.xtbl - row Shop_Names, PER-ROW reader (multi-row table)
// ===========================================================================
ShopName ParseShopName(const Node* row) {
    ShopName s;
    s.name = getText(row, "Name");
    s.localizedName = getText(row, "Localized_Name");
    const Node* ownership = sr3xtbl::FindChild(row, "Ownership");
    // Cost/Income/Discount/Total_Owner_Discount: "always" reads, not
    // "if present" - spec 12 CORRECTED (see tables.h): none of their
    // schema-stated defaults are loader-enforced, unlike Localized_Name's.
    s.cost = sr3xtbl::ReadInt32Always(ownership, "Cost");
    s.income = sr3xtbl::ReadInt32Always(ownership, "Income");
    s.discount = sr3xtbl::ReadFloatAlways(ownership, "Discount");
    s.totalOwnerDiscount = sr3xtbl::ReadFloatAlways(ownership, "Total_Owner_Discount");
    s.reward = getText(ownership, "Reward");
    s.useMessage = getText(row, "Use_Message");
    s.minimapIcon = getText(row, "Minimap_Icon");
    s.shopType = getText(row, "Shop_Type");
    return s;
}
std::vector<ShopName> ParseShopNamesTable(const Document& doc) {
    std::vector<ShopName> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Shop_Names")) out.push_back(ParseShopName(row));
    return out;
}

// ===========================================================================
// 9. The activity-text descriptor registry: fraud_text/snatch_text/
// snatch_kinzie_text/escort_text.xtbl - row element "String" (spec 9)
// ===========================================================================
ActivityText ParseActivityTextEntry(const Node* row) {
    ActivityText t;
    t.name = getText(row, "Name");
    t.english = getText(row, "English");
    t.displayText = getText(row, "DisplayText");
    t.duration = sr3xtbl::GetFloat(row, "Duration");
    return t;
}
ActivityTextTable ParseActivityText(const Node* row) {
    ActivityTextTable table;
    table.rowName = getText(row, "Name");
    const Node* texts = sr3xtbl::FindChild(row, "Texts");
    for (const Node* t : sr3xtbl::Children(texts, "Text")) table.texts.push_back(ParseActivityTextEntry(t));
    return table;
}
std::optional<ActivityTextTable> ParseActivityTextTable(const Document& doc) {
    const Node* row = sr3xtbl::FindChild(doc.table(), "String");
    if (!row) return std::nullopt;
    return ParseActivityText(row);
}

// ===========================================================================
// 8.2 horde_mode_text.xtbl - row Horde_Mode_Identifier (a DIFFERENT, flatter
// shape from the four activity-text files above - spec 8.2)
// ===========================================================================
HordeModeIdentifier ParseHordeModeIdentifier(const Node* row) {
    HordeModeIdentifier h;
    h.name = getText(row, "Name");
    return h;
}
std::vector<HordeModeIdentifier> ParseHordeModeTextTable(const Document& doc) {
    std::vector<HordeModeIdentifier> out;
    for (const Node* row : sr3xtbl::Children(doc.table(), "Horde_Mode_Identifier"))
        out.push_back(ParseHordeModeIdentifier(row));
    return out;
}

// ===========================================================================
// 10. fraud_globals.xtbl - row Fraud_Values
// ===========================================================================
namespace {

FraudMultiplierTier ParseFraudMultiplierTier(const Node* data, const std::string& minimumFieldName) {
    FraudMultiplierTier t;
    t.minimum = sr3xtbl::ReadFloatAlways(data, minimumFieldName);
    t.multiplierMin = sr3xtbl::ReadFloatAlways(data, "Multiplier_Min");
    t.multiplierMax = sr3xtbl::ReadFloatAlways(data, "Multiplier_Max");
    t.perMultiplier = sr3xtbl::ReadFloatAlways(data, "Per_Multiplier");
    return t;
}

// Parses one of the 12 tiered-multiplier categories (spec 10). Unlike an
// earlier version of this function, the three names below are NOT derived
// from a single "<prefix>_Multiplier[_Element]" / "Minimum_<prefix>"
// formula: spec 10's prose implies that uniform pattern, but real shipped
// fraud_globals.xtbl DATA (checked via this reader's own validation
// harness - never the executable) shows several categories deviate from it
// (pluralisation, noun substitution, and Windshield's irregular
// "_Cannon"-only-on-the-inner-element quirk) - see the long comment on
// FraudMultiplierCategory in tables.h for the full explanation and which
// names are CONFIRMED vs best-guess-by-pattern.
FraudMultiplierCategory ParseFraudMultiplierCategory(const Node* row, const std::string& elementWrapperName,
                                                       const std::string& multiplierElemName,
                                                       const std::string& minimumFieldName) {
    FraudMultiplierCategory c;
    const Node* flatMultiplier = sr3xtbl::FindChild(row, multiplierElemName);
    const Node* flatData = flatMultiplier ? sr3xtbl::FindChild(flatMultiplier, "Multiplier_Data") : nullptr;
    c.flatPresent = flatData != nullptr;
    c.flat = ParseFraudMultiplierTier(flatData, minimumFieldName);

    const Node* element = sr3xtbl::FindChild(row, elementWrapperName);
    c.tieredElementPresent = element != nullptr;
    c.cashInstead = sr3xtbl::ReadBoolAlways(element, "Cash_Instead");
    const Node* tieredGrid = sr3xtbl::FindChild(element, multiplierElemName);
    for (const Node* d : sr3xtbl::Children(tieredGrid, "Multiplier_Data"))
        c.tiers.push_back(ParseFraudMultiplierTier(d, minimumFieldName));
    return c;
}

}  // namespace

FraudGlobals ParseFraudGlobals(const Node* row) {
    FraudGlobals f;
    // Names below are CONFIRMED against the real shipped row except where
    // commented "pattern guess" (see the long comment on
    // FraudMultiplierCategory in tables.h).
    f.vehicle = ParseFraudMultiplierCategory(row, "Vehicle_Multiplier_Element", "Vehicle_Multiplier", "Minimum_Vehicles");
    f.airtime = ParseFraudMultiplierCategory(row, "Airtime_Multiplier_Element", "Airtime_Multiplier", "Minimum_Airtime");
    f.witness = ParseFraudMultiplierCategory(row, "Witness_Multiplier_Element" /* pattern guess: no tiered form in the real row */,
                                              "Witness_Multiplier", "Minimum_Witnesses");
    f.copWitness = ParseFraudMultiplierCategory(
        row, "Cop_Witness_Multiplier_Element" /* pattern guess: no tiered form in the real row */,
        "Cop_Witness_Multiplier", "Minimum_Cops");
    f.linearDist =
        ParseFraudMultiplierCategory(row, "Linear_Dist_Multiplier_Element", "Linear_Dist_Multiplier", "Minimum_Linear_Dist");
    f.civilVehicle = ParseFraudMultiplierCategory(
        row, "Civil_Vehicle_Multiplier_Element" /* pattern guess: no tiered form in the real row */,
        "Civil_Vehicle_Multiplier", "Minimum_Civil_Vehicles");
    f.crazyDriver =
        ParseFraudMultiplierCategory(row, "Crazy_Driver_Multiplier_Element", "Crazy_Driver_Multiplier", "Minimum_Crazy_Drivers");
    // Windshield: the "_Element" WRAPPER is "Windshield_Multiplier_Element"
    // (no "_Cannon"), but the flat/tiered-inner element is
    // "Windshield_Cannon_Multiplier" - a confirmed real-data irregularity.
    f.windshield =
        ParseFraudMultiplierCategory(row, "Windshield_Multiplier_Element", "Windshield_Cannon_Multiplier", "Minimum_Windshield_Cannon");
    f.surfing = ParseFraudMultiplierCategory(row, "Surfing_Multiplier_Element", "Surfing_Multiplier", "Minimum_Surfing");
    f.pinball = ParseFraudMultiplierCategory(row, "Pinball_Multiplier_Element" /* pattern guess: no tiered form in the real row */,
                                              "Pinball_Multiplier", "Minimum_Cars");
    f.cliffDiver = ParseFraudMultiplierCategory(
        row, "Cliff_Diver_Multiplier_Element" /* pattern guess: no tiered form in the real row */,
        "Cliff_Diver_Multiplier", "Minimum_Cliff_Distance");

    const Node* bodyMultipliers = sr3xtbl::FindChild(row, "Body_Multipliers");
    f.bodyMultipliersPresent = bodyMultipliers != nullptr;
    const Node* body = sr3xtbl::FindChild(bodyMultipliers, "Body");
    f.bodyMultipliers.head = sr3xtbl::ReadFloatAlways(body, "Head");
    f.bodyMultipliers.chest = sr3xtbl::ReadFloatAlways(body, "Chest");
    f.bodyMultipliers.groin = sr3xtbl::ReadFloatAlways(body, "Groin");
    f.bodyMultipliers.arm = sr3xtbl::ReadFloatAlways(body, "Arm");
    f.bodyMultipliers.leg = sr3xtbl::ReadFloatAlways(body, "Leg");

    // Luxury_Cars/Vehicles/Vehicle: exact 3-level path confirmed by spec 13's
    // cross-reference table ("fraud_globals.Luxury_Cars.Vehicles.Vehicle").
    const Node* luxuryCars = sr3xtbl::FindChild(row, "Luxury_Cars");
    const Node* luxuryVehicles = sr3xtbl::FindChild(luxuryCars, "Vehicles");
    for (const Node* v : sr3xtbl::Children(luxuryVehicles, "Vehicle"))
        if (v->text()) f.luxuryCars.push_back(*v->text());

    // Adrenaline_Grid / Adrenaline_During_Grid: row element "Adrenaline_Element"
    // for BOTH grids - confirmed against real shipped data (and the table's
    // own TableDescription, which documents "Name: Adrenaline_Element" for
    // both), not the "Tier" name an earlier version of this reader guessed.
    auto readCashBonusGrid = [&](const char* gridName) {
        std::vector<FraudCashBonusTier> out;
        const Node* grid = sr3xtbl::FindChild(row, gridName);
        for (const Node* t : sr3xtbl::Children(grid, "Adrenaline_Element")) {
            FraudCashBonusTier tier;
            tier.cash = sr3xtbl::ReadFloatAlways(t, "Cash");
            tier.bonus = sr3xtbl::ReadFloatAlways(t, "Bonus");
            out.push_back(tier);
        }
        return out;
    };
    f.adrenalineGrid = readCashBonusGrid("Adrenaline_Grid");
    f.adrenalineDuringGrid = readCashBonusGrid("Adrenaline_During_Grid");

    f.damageMultiplier = sr3xtbl::ReadFloatAlways(row, "Damage_Multiplier");
    f.moneyConversion = sr3xtbl::ReadFloatAlways(row, "Money_Conversion");
    f.minMoney = sr3xtbl::ReadFloatAlways(row, "Min_Money");
    f.newsVanType = getText(row, "News_Van_Type");
    f.witnessBonusRadius = sr3xtbl::ReadFloatAlways(row, "Witness_Bonus_Radius");
    f.onFireBonus = sr3xtbl::ReadFloatAlways(row, "On_Fire_Bonus");
    return f;
}
std::optional<FraudGlobals> ParseFraudGlobalsTable(const Document& doc) {
    const Node* row = sr3xtbl::FindChild(doc.table(), "Fraud_Values");
    if (!row) return std::nullopt;
    return ParseFraudGlobals(row);
}

// ===========================================================================
// 8.1 horde_mode.xtbl - row Horde_Mode (see horde_mode.h for scope caveats)
// ===========================================================================
HordeModeDefaultWeapon ParseHordeModeDefaultWeapon(const Node* row) {
    HordeModeDefaultWeapon w;
    w.name = getText(row, "Name");
    w.upgradedLevel = sr3xtbl::ReadInt32Always(row, "Upgraded_Level");
    return w;
}
HordeModeCustomizationItem ParseHordeModeCustomizationItem(const Node* row) {
    HordeModeCustomizationItem it;
    // The row ("Customization_Item") has CHILD elements (this one plus
    // Custom_Colors), so its own direct text is whitespace-only and reads as
    // absent (xtbl.h's TEXT rules); the item reference lives on a CHILD
    // element that happens to share the row's own tag name - read via
    // FindChild, not the row's own text.
    it.itemName = getText(row, "Customization_Item");
    const Node* colors = sr3xtbl::FindChild(row, "Custom_Colors");
    for (const Node* c : sr3xtbl::Children(colors, "Custom_Color")) {
        if (auto n = getText(c, "Color_Name")) it.customColorNames.push_back(*n);
    }
    return it;
}
HordeModeCustomization ParseHordeModeCustomization(const Node* block) {
    HordeModeCustomization c;
    c.gender = getText(block, "Gender");
    c.race = getText(block, "Race");
    c.figure = getText(block, "Figure");
    c.persona = getText(block, "Persona");
    c.outfit = getText(block, "Outfit");
    const Node* items = sr3xtbl::FindChild(block, "Customization_Items");
    c.customizationItemsPresent = items != nullptr;
    for (const Node* it : sr3xtbl::Children(items, "Customization_Item"))
        c.customizationItems.push_back(ParseHordeModeCustomizationItem(it));
    return c;
}
HordeModePlayerCharacterLines ParseHordeModePlayerCharacterLines(const Node* block) {
    HordeModePlayerCharacterLines l;
    l.death = getText(block, "Death");
    l.waveVictory = getText(block, "Wave_Victory");
    l.levelVictory = getText(block, "Level_Victory");
    return l;
}
HordeModePlayerCharacter ParseHordeModePlayerCharacter(const Node* row) {
    HordeModePlayerCharacter p;
    p.name = getText(row, "Name");
    p.displayName = getText(row, "display_name");
    const Node* customization = sr3xtbl::FindChild(row, "Customization");
    p.customizationPresent = customization != nullptr;
    p.customization = ParseHordeModeCustomization(customization);
    p.sprintTimeSeconds = sr3xtbl::ReadFloatAlways(row, "Sprint_Time_Seconds");
    p.health = sr3xtbl::ReadFloatAlways(row, "Health");
    p.healthRechargeRate = sr3xtbl::ReadFloatAlways(row, "Health_Recharge_Rate");
    const Node* weapons = sr3xtbl::FindChild(row, "Default_Weapons");
    for (const Node* w : sr3xtbl::Children(weapons, "Default_Weapon"))
        p.defaultWeapons.push_back(ParseHordeModeDefaultWeapon(w));
    p.lines = ParseHordeModePlayerCharacterLines(sr3xtbl::FindChild(row, "Lines"));
    return p;
}
HordeModeLevel ParseHordeModeLevel(const Node* row) {
    HordeModeLevel l;
    l.uniqueId = getText(row, "Unique_ID");
    l.filename = getText(sr3xtbl::FindChild(row, "xtbl"), "Filename");
    l.displayName = getText(row, "display_name");
    return l;
}
HordeModeCoopMultiplayerConstants ParseHordeModeCoopMultiplayerConstants(const Node* block) {
    HordeModeCoopMultiplayerConstants c;
    c.base.enemyHealth = sr3xtbl::ReadFloatAlways(block, "Enemy_Health");
    c.base.enemyDamage = sr3xtbl::ReadFloatAlways(block, "Enemy_Damage");
    const Node* multipliers = sr3xtbl::FindChild(block, "Multipliers");
    c.multipliers.enemyHealth = sr3xtbl::ReadFloatAlways(multipliers, "Enemy_Health");
    c.multipliers.enemyDamage = sr3xtbl::ReadFloatAlways(multipliers, "Enemy_Damage");
    c.reviveLimit = sr3xtbl::ReadInt32Always(block, "Revive_Limit");
    c.reviveResetWaveInterval = sr3xtbl::ReadInt32Always(block, "Revive_Reset_Wave_Interval");
    return c;
}
HordeModeSpecialSpawnCondition ParseHordeModeSpecialSpawnCondition(const Node* row) {
    HordeModeSpecialSpawnCondition s;
    s.name = getText(row, "Name");
    const Node* data = sr3xtbl::FindChild(row, "Data");
    static constexpr std::array<std::string_view, 4> kVariantNames = {"Kills", "Points", "Wave_Number",
                                                                        "Every_N_Waves"};
    const Node* variantNode = nullptr;
    for (std::string_view vn : kVariantNames) {
        if (const Node* n = sr3xtbl::FindChild(data, vn)) {
            variantNode = n;
            s.variant = std::string(vn);
            break;
        }
    }
    s.data.repeat = sr3xtbl::ReadBoolAlways(variantNode, "Repeat");
    s.data.count = sr3xtbl::ReadInt32Always(variantNode, "Count");
    s.data.number = sr3xtbl::ReadInt32Always(variantNode, "Number");
    return s;
}
HordeModePowerup ParseHordeModePowerup(const Node* row) {
    HordeModePowerup p;
    p.type = getText(row, "Type");
    p.itemName = getText(row, "Item_Name");
    p.audioCue = getText(row, "Audio_Cue");
    return p;
}
HordeModeEnemyData ParseHordeModeEnemyData(const Node* block) {
    HordeModeEnemyData e;
    const Node* ai = sr3xtbl::FindChild(block, "Default_AI_Settings");
    e.defaultAiSettingsPresent = ai != nullptr;
    e.defaultAiSettings.personality = getText(ai, "Personality");
    e.defaultAiSettings.behavior = getText(ai, "Behavior");
    const Node* pointValues = sr3xtbl::FindChild(block, "Point_Values");
    for (const Node* pv : sr3xtbl::Children(pointValues, "Point_Value")) {
        HordeModePointValue v;
        v.name = getText(pv, "Name");
        v.points = sr3xtbl::ReadInt32Always(pv, "Value");  // CONFIRMED real tag "Value", see horde_mode.h
        e.pointValues.push_back(v);
    }
    return e;
}
HordeModePointMultipliers ParseHordeModePointMultipliers(const Node* block) {
    HordeModePointMultipliers pm;
    const Node* weapons = sr3xtbl::FindChild(block, "Weapons");
    for (const Node* w : sr3xtbl::Children(weapons, "Weapon")) {
        HordeModeWeaponMultiplier wm;
        wm.name = getText(w, "Name");
        wm.multiplier = sr3xtbl::ReadFloatAlways(w, "Point_Multiplier");  // CONFIRMED real tag, see horde_mode.h
        pm.weapons.push_back(wm);
    }
    pm.meleePointMultiplier = sr3xtbl::ReadFloatAlways(block, "Melee_Point_Multiplier");
    return pm;
}
HordeMode ParseHordeMode(const Node* row) {
    HordeMode h;
    const Node* uiSettings = sr3xtbl::FindChild(row, "UI_Settings");
    const Node* floatingPoints = sr3xtbl::FindChild(uiSettings, "Floating_Points");
    h.floatingPoints.intensity = sr3xtbl::ReadFloatAlways(floatingPoints, "Intensity");
    h.floatingPoints.scale = sr3xtbl::ReadFloatAlways(floatingPoints, "Scale");
    h.floatingPoints.locationScale = sr3xtbl::ReadFloatAlways(floatingPoints, "Location_Scale");

    h.waveFailsafeCompletionDelayMs = sr3xtbl::ReadInt32Always(row, "Wave_Failsafe_Completion_Delay_ms");
    h.waveFailsafeMinEnemyKillsPct =
        sr3xtbl::ReadFloatAlways(row, "Wave_Failsafe_Completion_Min_Enemy_Kills_Pct");
    h.waveFailsafeNoEnemiesRadius = sr3xtbl::ReadFloatAlways(row, "Wave_Failsafe_Completion_No_Enemies_Radius");

    const Node* levels = sr3xtbl::FindChild(row, "Levels");
    for (const Node* l : sr3xtbl::Children(levels, "Level")) h.levels.push_back(ParseHordeModeLevel(l));

    const Node* playerCharacters = sr3xtbl::FindChild(row, "Player_Characters");
    for (const Node* p : sr3xtbl::Children(playerCharacters, "Player_Character"))
        h.playerCharacters.push_back(ParseHordeModePlayerCharacter(p));

    const Node* coop = sr3xtbl::FindChild(row, "Cooperative_Multiplayer_Constants");
    h.coopMultiplayerConstantsPresent = coop != nullptr;
    h.coopMultiplayerConstants = ParseHordeModeCoopMultiplayerConstants(coop);

    const Node* spawnConditions = sr3xtbl::FindChild(row, "Special_Spawn_Conditions");
    for (const Node* s : sr3xtbl::Children(spawnConditions, "Special_Spawn_Condition"))
        h.specialSpawnConditions.push_back(ParseHordeModeSpecialSpawnCondition(s));

    const Node* powerups = sr3xtbl::FindChild(row, "Powerups");
    for (const Node* p : sr3xtbl::Children(powerups, "Powerup")) h.powerups.push_back(ParseHordeModePowerup(p));

    const Node* enemyData = sr3xtbl::FindChild(row, "Enemy_Data");
    h.enemyDataPresent = enemyData != nullptr;
    h.enemyData = ParseHordeModeEnemyData(enemyData);

    const Node* pointMultipliers = sr3xtbl::FindChild(row, "Point_Multipliers");
    h.pointMultipliersPresent = pointMultipliers != nullptr;
    h.pointMultipliers = ParseHordeModePointMultipliers(pointMultipliers);

    return h;
}
std::optional<HordeMode> ParseHordeModeTable(const Document& doc) {
    const Node* row = sr3xtbl::FindChild(doc.table(), "Horde_Mode");
    if (!row) return std::nullopt;
    return ParseHordeMode(row);
}

}  // namespace sr3tables_diversions
