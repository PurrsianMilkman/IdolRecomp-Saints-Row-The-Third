// lua_spec_confirmed_stubs_b29_sec6_notoriety_mission.cpp
//
// Batch b29 (2026-10-08): closes out spec-lua-api-behaviour.md Section 6
// ("Mission/trigger/notoriety/spawn cluster, 21 functions") - the 10 of its
// 21 names an earlier Section-6 batch deliberately deferred (see
// engine_state.h's own "--- Section 6 batch (2026-10-07...)" top comment,
// now updated to point here): disable_specific_notoriety_group (Sec6.2),
// notoriety_set_min_and_max (Sec6.3), notoriety_set_max (Sec6.5),
// notoriety_set (Sec6.7), notoriety_force_no_spawn_group (Sec6.10),
// notoriety_set_min (Sec6.11), mission_end_success (Sec6.12),
// notoriety_reset (Sec6.16), mission_end_to_activity (Sec6.20),
// mission_set_cancel_warp_location (Sec6.21). tutorial_start (Sec6.19)
// stays OUT of scope - its 210-entry descriptor table and 12-step
// dispatcher remain a disproportionate new subsystem for one native.
//
// WHY THE EARLIER DEFERRAL IS RESOLVED, NOT OVERRIDDEN: the earlier batch's
// own stated blockers were (a) "need a real alias/group-name table this
// spec text does not supply" for the 7 notoriety names, and (b) "no model
// for the mission-active gate" for the 3 mission_end_*/cancel_warp names.
// Neither blocker required inventing data:
//   (a) this project's OWN established idiom for "a CONFIRMED resolver
//       mechanism whose full backing table is not given by spec" is a
//       test-registrable map with a deterministic not-found behaviour
//       (lookupNotorietyTrackCode's own 0x0094cc60/sentinel-9 resolver,
//       already shipped in this same header, is exactly this shape) - the
//       SAME idiom is applied here to the 0x00605160 alias resolver
//       (sentinel 5) and reuses notorietySpawnGroupResolves()/
//       getOrCreateNotorietySpawnGroup() (0x008ed350) outright for the two
//       group-table names, since Batch 4 (2026-10-08, earlier the same
//       day) already modeled that EXACT table for notoriety_spawn_group_*.
//   (b) MissionContext (Sec1.5/1.6) was implemented by the Team-A-cleared
//       batch, landed to main ahead of this one - the "mission is active"
//       gate these three share with mission_end_failure already exists.
//
// See engine_state.h's own doc comments (notorietyAliasTrackCode*,
// NotorietySpawnGroupState::forceNoSpawn/tierItemFlags,
// notorietyGroupTierInRangeResolves, MissionContext's new fields,
// MissionCancelWarpLocation) for the full per-field citations; this file's
// own stub comments cite only what each native's own body does with them.
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

namespace sr3luahost {
namespace {

// ---------------------------------------------------------------------
// disable_specific_notoriety_group (Sec6.2, 0x00a47330). CONFIRMED: 4
// mandatory args, none nil-gated - arg1 GROUP name (notorietySpawnGroupResolves(),
// the SAME 0x008ed350 table notoriety_spawn_group_set_delay/_destination
// already use), arg2 number (rounded via truncateEa2596, a tier index),
// arg3 string (an item name within that tier), arg4 boolean (the value to
// write). Return: 1 boolean-shaped value - true on a successful write,
// false (0) on ANY failure path (unresolved group, out-of-range tier, no
// item match - none of these are individually distinguished by the real
// function's own return value either). 0x00d21db0 runs unconditionally
// first on a resolved group (identity OPEN, Sec6.2's own text - no
// modeled effect). The per-group tier MIN/MAX bounds (+0x3e0/+0x3e4) are
// not given by spec for ANY of the 24 groups (not even "police") - gated
// behind notorietyGroupTierInRangeResolves(), OPEN until a test sets it.
// ---------------------------------------------------------------------
int stub_disable_specific_notoriety_group(lua_State* L) {
    logCall(L, upLog(L), "disable_specific_notoriety_group", upStateTag(L));
    std::string groupName = argString(L, 1);
    int64_t tier = truncateEa2596(lua_tonumber(L, 2)); // CONFIRMED: mandatory, no nil-gate, Sec6.2
    std::string itemName = argString(L, 3);
    bool value = lua_toboolean(L, 4) != 0; // CONFIRMED: mandatory, no nil-gate, Sec6.2
    EngineState* st = upState(L);
    if (!st->notorietySpawnGroupResolves().get(groupName)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    std::string tierKey = groupName + "#" + std::to_string(tier);
    if (!st->notorietyGroupTierInRangeResolves().get(tierKey)) { // OPEN until set - per-group bounds not given
        lua_pushboolean(L, 0);
        return 1;
    }
    auto& group = st->getOrCreateNotorietySpawnGroup(groupName);
    group.tierItemFlags[tier][itemName] = value;
    ++group.disableSpecificGroupWriteCount;
    lua_pushboolean(L, 1);
    return 1;
}

// ---------------------------------------------------------------------
// notoriety_set_min_and_max (Sec6.3, 0x00a55b80). CONFIRMED: 3 mandatory
// args, no nil-gates - arg1 track name (lookupNotorietyAliasTrackCode(),
// the 0x00605160 resolver), arg2/arg3 numbers (min/max). CONFIRMED: this
// entry DOES check the resolver's own sentinel 5 and skips the write
// entirely on no match (unlike notoriety_set_max/notoriety_set/
// notoriety_set_min below). Host: writes both fields directly; non-host:
// EngineState::replicateStateChange() only (Sec6.22's own host-check
// citation for "every record-and-replicate call site newly traced in
// THIS cluster"). Return: none.
// ---------------------------------------------------------------------
int stub_notoriety_set_min_and_max(lua_State* L) {
    logCall(L, upLog(L), "notoriety_set_min_and_max", upStateTag(L));
    std::string trackName = argString(L, 1);
    double minV = lua_tonumber(L, 2);
    double maxV = lua_tonumber(L, 3);
    EngineState* st = upState(L);
    int32_t code = st->lookupNotorietyAliasTrackCode(trackName);
    if (code == EngineState::kNotorietyAliasTrackNotFound) return 0; // CONFIRMED sentinel-checked skip, Sec6.3
    auto& track = st->notorietyAliasTrackStates()[code];
    if (st->coopLocalIsHost()) {
        track.minValue = minV;
        track.maxValue = maxV;
    } else {
        EngineState::replicateStateChange("notoriety_set_min_and_max", trackName);
    }
    ++track.setMinAndMaxCount;
    return 0;
}

// ---------------------------------------------------------------------
// notoriety_set_max (Sec6.5, 0x00a55ac0). CONFIRMED: 2 mandatory args, no
// nil-gates. Unlike Sec6.3 above, this entry does NOT check the resolver's
// own sentinel at all - an unrecognized name still writes using code 5
// unconditionally (CONFIRMED, Sec6.5's own text: "this entry point does
// not check the resolver's own 5 failure sentinel at all"). Same host-
// gate/replicate shape as Sec6.3. Return: none.
// ---------------------------------------------------------------------
int stub_notoriety_set_max(lua_State* L) {
    logCall(L, upLog(L), "notoriety_set_max", upStateTag(L));
    std::string trackName = argString(L, 1);
    double maxV = lua_tonumber(L, 2);
    EngineState* st = upState(L);
    int32_t code = st->lookupNotorietyAliasTrackCode(trackName); // CONFIRMED: no sentinel guard here, Sec6.5
    auto& track = st->notorietyAliasTrackStates()[code];
    if (st->coopLocalIsHost()) {
        track.maxValue = maxV;
    } else {
        EngineState::replicateStateChange("notoriety_set_max", trackName);
    }
    ++track.setMaxCount;
    return 0;
}

// ---------------------------------------------------------------------
// notoriety_set (Sec6.7, 0x00a550b0). CONFIRMED: 2 mandatory args, no
// nil-gates, no sentinel guard (same shape as Sec6.5/Sec6.11). Sets the
// track's CURRENT value (sub-tag 5, 0x006079c0). Return: none.
// ---------------------------------------------------------------------
int stub_notoriety_set(lua_State* L) {
    logCall(L, upLog(L), "notoriety_set", upStateTag(L));
    std::string trackName = argString(L, 1);
    double value = lua_tonumber(L, 2);
    EngineState* st = upState(L);
    int32_t code = st->lookupNotorietyAliasTrackCode(trackName); // CONFIRMED: no sentinel guard, Sec6.7
    auto& track = st->notorietyAliasTrackStates()[code];
    if (st->coopLocalIsHost()) {
        track.currentValue = value;
    } else {
        EngineState::replicateStateChange("notoriety_set", trackName);
    }
    ++track.setCount;
    return 0;
}

// ---------------------------------------------------------------------
// notoriety_force_no_spawn_group (Sec6.10, 0x00a54f00). CONFIRMED: 2
// mandatory args, no nil-gates - arg1 GROUP name (notorietySpawnGroupResolves(),
// the SAME 0x008ed350 table as Sec6.2), arg2 boolean. A single,
// unconditional, UNGATED instruction write (0x008ecda0, the SAME `+0x455`
// flag-byte setter notoriety_force_no_spawn/Sec3.6 already uses) - no
// host-check, no replicate (CONFIRMED, Sec6.10's own text: "called here
// exactly once... implicit this and arg 2 as the value", no gate
// mentioned, unlike the five alias-track setters above). Return: none.
// ---------------------------------------------------------------------
int stub_notoriety_force_no_spawn_group(lua_State* L) {
    logCall(L, upLog(L), "notoriety_force_no_spawn_group", upStateTag(L));
    std::string groupName = argString(L, 1);
    bool value = lua_toboolean(L, 2) != 0; // CONFIRMED: no nil-gate, Sec6.10
    EngineState* st = upState(L);
    if (!st->notorietySpawnGroupResolves().get(groupName)) return 0; // OPEN until set
    auto& group = st->getOrCreateNotorietySpawnGroup(groupName);
    group.forceNoSpawn = value; // CONFIRMED: single unconditional ungated write, Sec6.10
    ++group.forceNoSpawnSetCount;
    return 0;
}

// ---------------------------------------------------------------------
// notoriety_set_min (Sec6.11, 0x00a55b20). CONFIRMED: 2 mandatory args, no
// nil-gates, no sentinel guard (same shape as Sec6.5/Sec6.7). Sets the
// track's MIN value (sub-tag 2, 0x00607ea0). Return: none.
// ---------------------------------------------------------------------
int stub_notoriety_set_min(lua_State* L) {
    logCall(L, upLog(L), "notoriety_set_min", upStateTag(L));
    std::string trackName = argString(L, 1);
    double minV = lua_tonumber(L, 2);
    EngineState* st = upState(L);
    int32_t code = st->lookupNotorietyAliasTrackCode(trackName); // CONFIRMED: no sentinel guard, Sec6.11
    auto& track = st->notorietyAliasTrackStates()[code];
    if (st->coopLocalIsHost()) {
        track.minValue = minV;
    } else {
        EngineState::replicateStateChange("notoriety_set_min", trackName);
    }
    ++track.setMinCount;
    return 0;
}

// ---------------------------------------------------------------------
// mission_end_success (Sec6.12, 0x00a536b0). CONFIRMED: up to 3 args -
// arg1 mandatory string (passed straight into the SAME gate function
// mission_end_failure/Sec1.5 uses, 0x006d7240), arg2 optional string
// (success-announcement text, consumed for call shape only, same as
// mission_end_failure's own arg2), arg3 optional polymorphic - a table
// (first 2 indexed elements, each read as a string) or a plain string
// (first slot only) or absent (both slots empty). Gate: IDENTICAL 3-
// condition gate mission_end_failure uses (MissionContext::
// activeAndRunning/currentMissionName/currentMissionEnding). On a pass:
// calls the SAME final dispatch (0x006da400) mission_end_failure uses,
// arming the SAME shared timer - tracked under mission_end_success's own
// counter so a test can tell the two callers apart. Each resolved arg3
// name's position-pair would anchor the UI; this project has no real
// transform data (same convention mission_set_checkpoint/Sec1.6 already
// established), so only whether each slot resolved is recorded. Return:
// none (both the gated-out and normal paths, per spec).
// ---------------------------------------------------------------------
int stub_mission_end_success(lua_State* L) {
    logCall(L, upLog(L), "mission_end_success", upStateTag(L));
    std::string missionName = argString(L, 1); // CONFIRMED: passed straight to the shared gate, Sec6.12/Sec1.5
    (void)optionalStringDefault(L, 2, ""); // success-announcement text - call-shape only, same as mission_end_failure's arg2
    EngineState* st = upState(L);
    auto& mc = st->missionContext();
    // FIX 2026-10-08 (mission-context-name task): all three reads below are
    // now set for real by EngineState::bindMissionPhaseMachine() at mission-
    // bind time (0x006d6920, Sec21.27a/MissionContext's own field doc
    // comments) - a real mission reaching this native no longer throws
    // OpenStateError here; it correctly fails closed (silent no-op) until
    // something flips currentMissionEnding true.
    bool gatePasses = mc.activeAndRunning.get() &&
                      mc.currentMissionName.get() == missionName &&
                      mc.currentMissionEnding.get();
    bool slot1Resolved = false, slot2Resolved = false;
    if (lua_type(L, 3) == LUA_TTABLE) {
        lua_rawgeti(L, 3, 1);
        std::string n1 = argString(L, -1);
        lua_pop(L, 1);
        lua_rawgeti(L, 3, 2);
        std::string n2 = argString(L, -1);
        lua_pop(L, 1);
        if (!n1.empty()) slot1Resolved = st->objectResolves().get(n1); // OPEN until set
        if (!n2.empty()) slot2Resolved = st->objectResolves().get(n2); // OPEN until set
    } else if (lua_type(L, 3) == LUA_TSTRING) {
        std::string n1 = argString(L, 3);
        if (!n1.empty()) slot1Resolved = st->objectResolves().get(n1); // OPEN until set
    }
    if (gatePasses) {
        mc.missionEndTimer1Armed = true; // SAME shared dispatch/timer mission_end_failure arms, 0x006da400 (Sec6.12 cross-ref)
        ++mc.missionEndSuccessDispatchCount;
        mc.lastMissionEndSuccessSlot1Resolved = slot1Resolved;
        mc.lastMissionEndSuccessSlot2Resolved = slot2Resolved;
    }
    return 0;
}

// ---------------------------------------------------------------------
// notoriety_reset (Sec6.16, 0x00a54ff0). CONFIRMED: 1 mandatory string, no
// nil-gate. DOES check the resolver's own sentinel 5 (skips everything on
// no match, same guarded shape as Sec6.3). On success, THREE calls:
// (1) 0x00609810 - a wholesale notoriety UI/meter reset, CONFIRMED host-
//     only with NO replicated fallback at all on a non-host machine (a
//     genuine, confirmed asymmetry against its four sibling setters -
//     EngineState::recordNotorietyUiMeterReset()).
// (2)/(3) 0x00607ea0 and 0x006080d0 (the SAME "set min"/"set max" setters
//     Sec6.11/Sec6.5 use) are BOTH called with the literal value
//     0xffffffff - going through the SAME host-gate/replicate shape those
//     two setters themselves use.
// Return: none.
// ---------------------------------------------------------------------
int stub_notoriety_reset(lua_State* L) {
    logCall(L, upLog(L), "notoriety_reset", upStateTag(L));
    std::string trackName = argString(L, 1);
    EngineState* st = upState(L);
    int32_t code = st->lookupNotorietyAliasTrackCode(trackName);
    if (code == EngineState::kNotorietyAliasTrackNotFound) return 0; // CONFIRMED sentinel-checked skip, Sec6.16
    if (st->coopLocalIsHost()) {
        st->recordNotorietyUiMeterReset(); // CONFIRMED: host-only, NO replicated fallback at all
    }
    auto& track = st->notorietyAliasTrackStates()[code];
    constexpr double kResetSentinel = 4294967295.0; // 0xffffffff as an unsigned 32-bit value, CONFIRMED literal (Sec6.16)
    if (st->coopLocalIsHost()) {
        track.minValue = kResetSentinel;
        track.maxValue = kResetSentinel;
    } else {
        EngineState::replicateStateChange("notoriety_reset_min", trackName);
        EngineState::replicateStateChange("notoriety_reset_max", trackName);
    }
    ++track.resetCount;
    return 0;
}

// ---------------------------------------------------------------------
// mission_end_to_activity (Sec6.20, 0x00a53890). CONFIRMED: 2 mandatory
// strings, no nil-gates - arg1 mission id (gated through the SAME gate
// function mission_end_failure/mission_end_success use), arg2 activity
// name (a SIXTH catalogued member of the type-gated resolver family,
// bit 0x4@+0xc - this project's flat objectResolves() stand-in, same
// project-wide simplification every per-kind resolver already uses).
// Gate fails -> nothing happens at all (CONFIRMED, same as mission_end_
// failure's own gated-out path). Gate passes: arg2 is attempted
// regardless of outcome; 0x006da960 copies the resolved object's
// position-pair into two globals ONLY when (resolved AND host) - no real
// transform data modeled, so only whether the capture happened is
// counted; 0x006da7b0 itself runs UNCONDITIONALLY once the gate passes
// (CONFIRMED, Sec6.20's own text) - its own internal behaviour is OPEN,
// not modeled beyond "it ran." Return: none.
// ---------------------------------------------------------------------
int stub_mission_end_to_activity(lua_State* L) {
    logCall(L, upLog(L), "mission_end_to_activity", upStateTag(L));
    std::string missionName = argString(L, 1);
    std::string activityName = argString(L, 2);
    EngineState* st = upState(L);
    auto& mc = st->missionContext();
    // FIX 2026-10-08 (mission-context-name task): all three reads below are
    // now set for real by EngineState::bindMissionPhaseMachine() at mission-
    // bind time (0x006d6920, Sec21.27a/MissionContext's own field doc
    // comments) - a real mission reaching this native no longer throws
    // OpenStateError here; it correctly fails closed (silent no-op) until
    // something flips currentMissionEnding true.
    bool gatePasses = mc.activeAndRunning.get() &&
                      mc.currentMissionName.get() == missionName &&
                      mc.currentMissionEnding.get();
    if (!gatePasses) return 0; // CONFIRMED: nothing happens at all on a gate miss
    bool resolved = st->objectResolves().get(activityName); // OPEN until set - the "activity" object-kind resolve, Sec6.20
    if (resolved && st->coopLocalIsHost()) {
        ++mc.missionEndToActivityPositionCaptureCount; // test-observable only - no real transform system, Sec6.20
    }
    ++mc.missionEndToActivityDispatchCount; // 0x006da7b0 runs unconditionally once the gate passes, Sec6.20
    return 0;
}

// ---------------------------------------------------------------------
// mission_set_cancel_warp_location (Sec6.21, 0x00a53900). CONFIRMED: 2
// optional strings, both standard nil-gated idiom, default absent. Each,
// if present, resolves via objectResolves() (0x005982e0, the SAME third-
// tier resolver door_open/teleport's own destination use) gated by the
// SAME "mission active, phase != 8" predicate (MissionContext::
// activeAndRunning - the identical two globals 0x014c8460/0x014c7a14 per
// Sec6.21's own cross-reference). Both absent/unresolved -> clears the
// override (gated on missionActive); BOTH resolved -> sets it (gated on
// missionActive); EXACTLY ONE resolved -> CONFIRMED no-op, neither path
// runs (not even the mission-active check). Return: none.
// ---------------------------------------------------------------------
int stub_mission_set_cancel_warp_location(lua_State* L) {
    logCall(L, upLog(L), "mission_set_cancel_warp_location", upStateTag(L));
    std::string fromName = optionalStringDefault(L, 1, "");
    std::string toName = optionalStringDefault(L, 2, "");
    EngineState* st = upState(L);
    bool fromResolved = !fromName.empty() && st->objectResolves().get(fromName); // OPEN until set
    bool toResolved = !toName.empty() && st->objectResolves().get(toName);       // OPEN until set
    auto& warp = st->missionCancelWarpLocation();
    if (!fromResolved && !toResolved) {
        if (st->missionContext().activeAndRunning.get()) { // OPEN until set
            warp.set = false;
            warp.fromName.clear();
            warp.toName.clear();
            ++warp.clearCount;
        }
    } else if (fromResolved && toResolved) {
        if (st->missionContext().activeAndRunning.get()) { // OPEN until set
            warp.set = true;
            warp.fromName = fromName;
            warp.toName = toName;
            ++warp.setCount;
        }
    } else {
        ++warp.partialNoOpCount; // CONFIRMED: exactly one resolved - neither path runs, Sec6.21
    }
    return 0;
}

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB29() {
    static const std::vector<std::string> names = {
        "disable_specific_notoriety_group",
        "notoriety_set_min_and_max",
        "notoriety_set_max",
        "notoriety_set",
        "notoriety_force_no_spawn_group",
        "notoriety_set_min",
        "mission_end_success",
        "notoriety_reset",
        "mission_end_to_activity",
        "mission_set_cancel_warp_location",
    };
    return names;
}

void registerB29(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    // All 10 are `gameplay` per the 1,014-entry gameplay table citation
    // Sec6's own scope note gives for this whole cluster (tools/
    // lua_all_registered_1430_tagged.txt).
    if (wants("disable_specific_notoriety_group"))
        registerOne(L, state, log, stateTag, "disable_specific_notoriety_group", openGuard<stub_disable_specific_notoriety_group>);
    if (wants("notoriety_set_min_and_max"))
        registerOne(L, state, log, stateTag, "notoriety_set_min_and_max", openGuard<stub_notoriety_set_min_and_max>);
    if (wants("notoriety_set_max"))
        registerOne(L, state, log, stateTag, "notoriety_set_max", openGuard<stub_notoriety_set_max>);
    if (wants("notoriety_set"))
        registerOne(L, state, log, stateTag, "notoriety_set", openGuard<stub_notoriety_set>);
    if (wants("notoriety_force_no_spawn_group"))
        registerOne(L, state, log, stateTag, "notoriety_force_no_spawn_group", openGuard<stub_notoriety_force_no_spawn_group>);
    if (wants("notoriety_set_min"))
        registerOne(L, state, log, stateTag, "notoriety_set_min", openGuard<stub_notoriety_set_min>);
    if (wants("mission_end_success"))
        registerOne(L, state, log, stateTag, "mission_end_success", openGuard<stub_mission_end_success>);
    if (wants("notoriety_reset"))
        registerOne(L, state, log, stateTag, "notoriety_reset", openGuard<stub_notoriety_reset>);
    if (wants("mission_end_to_activity"))
        registerOne(L, state, log, stateTag, "mission_end_to_activity", openGuard<stub_mission_end_to_activity>);
    if (wants("mission_set_cancel_warp_location"))
        registerOne(L, state, log, stateTag, "mission_set_cancel_warp_location", openGuard<stub_mission_set_cancel_warp_location>);
}

} // namespace stub_sections
} // namespace sr3luahost
