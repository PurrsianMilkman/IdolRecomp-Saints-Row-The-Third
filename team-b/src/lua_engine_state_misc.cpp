// lua_engine_state_misc.cpp
// EngineState member bodies: data responders, scripted requests, vehicle pathfind,
// tutorials, DLC2 clapboards, co-op, name hashes, zscene keys, pause map, spawn
// region/time of day, QTE, open-slot inventory, CAE actions and script request ids,
// roadblocks, despawn-all, machinima paths, zone trigger geometry.
//
// Moved verbatim out of lua_engine_state.cpp (Team C refactor, 2026-10-17); see
// engine_state.h for every member's own documentation.

#include "sr3luahost/engine_state.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>

#include "sr3luahost/save_game_bridge.h"
#include "sr3luahost/vint_property_types.h"
#include "sr3save/save_crc.h"
#include "sr3vintdoc/vint_doc.h"

namespace sr3luahost {

// Design notes moved verbatim from EngineState::registerDataResponderForTesting's declaration in engine_state.h
// (refactor step 3a):
// --- vint_dataresponder_finished / vint_internal_dataresponder_request
// (Sec21) -----------------------------------------------------------

// Test/setup-only entry point (see VintDataResponderRecord's own doc
// comment on why no Lua-visible function in this pass's 2-name scope
// populates this registry).
void EngineState::registerDataResponderForTesting(const std::string& name, bool finished) {
    vintDataResponders_[sr3save::nameHash(name)] = VintDataResponderRecord{finished, 0};
}

bool EngineState::dataResponderFinished(const std::string& name) const {
    auto it = vintDataResponders_.find(sr3save::nameHash(name));
    if (it == vintDataResponders_.end()) return true; // CONFIRMED real quirk (Sec21.1): no record at all -> "finished"
    return it->second.finished;
}

// Design notes moved verbatim from EngineState::dataResponderRequest's declaration in engine_state.h
// (refactor step 3a):
// CONFIRMED (Sec21.2): a complete, silent no-op when no record exists
// for `name`, or when `validCallback`/`validMax` (the caller's own
// lua_type checks on args 2/3) are false - never creates the record
// itself (Sec21.2's own text: "this native never creates the named
// record"). When a record exists and both types validate, increments
// this project's own dispatchAttempts counter (test-observable proof the
// gate runs) - the real further dispatch (0x00e1e660, the trailing-
// argument array, and the +0x25c context stamp) has no Lua-visible
// consequence among this pass's two in-scope functions and is not
// modeled (0x00e1e660's own body was not independently decompiled this
// pass either, per Sec21.2's own explicit "not traced" note).
void EngineState::dataResponderRequest(const std::string& name, bool validCallback, bool validMax) {
    auto it = vintDataResponders_.find(sr3save::nameHash(name));
    if (it == vintDataResponders_.end()) return; // CONFIRMED (Sec21.2): no record -> complete, silent no-op
    if (!validCallback || !validMax) return;       // CONFIRMED: wrong/missing arg type -> complete, silent no-op
    ++it->second.dispatchAttempts; // test-observable only - see this function's own header doc comment
}

int EngineState::dataResponderDispatchAttempts(const std::string& name) const {
    auto it = vintDataResponders_.find(sr3save::nameHash(name));
    return it == vintDataResponders_.end() ? 0 : it->second.dispatchAttempts;
}

void EngineState::registerScriptedRequestForTesting(const std::string& name, int kind, bool done) {
    scriptedRequestDone_[{name, kind}] = done;
}

int EngineState::scriptedRequestStatusCode(const std::string& name, int kind) {
    auto it = scriptedRequestDone_.find({name, kind});
    if (it == scriptedRequestDone_.end()) return 2; // Sec35.2: no matching request
    if (!it->second) return 0;                      // Sec35.2: request exists, still pending
    scriptedRequestDone_.erase(it);                  // Sec35.2: a "done" read releases the record
    return 1;
}

// Design notes moved verbatim from EngineState::setVehiclePathfindResolvableForTesting's declaration in engine_state.h
// (refactor step 3a):
// vehicle_pathfind_check_done (Sec9.10, the REFERENCE reading Sec35.2's
// own cross-check cites as "already correct"): the real wrapper resolves
// the vehicle AND checks for an occupant/part in slot 0 (most plausibly a
// driver check, Sec9.10's own text) BEFORE ever consulting the shared
// pool; failing either converges on the SAME literal fallback constant
// the pool's own "no request" code already uses, 2.0 (Sec9.10: "the SAME
// numeric code the success path itself uses... both routes converge on
// one consistent answer"). This project has no vehicle-resolution/
// occupant registry (same "no invented entity system" boundary as
// CharacterState's own top note) - every name defaults UNRESOLVABLE,
// which is itself the real, CONFIRMED fallback path for an unmodeled
// vehicle, not a guess. setVehiclePathfindResolvableForTesting() is the
// only way to open the gate, so a test can still exercise the shared
// pool's own 0/1/2 polarity through this wrapper.
void EngineState::setVehiclePathfindResolvableForTesting(const std::string& vehicleName, bool resolvable) {
    if (resolvable) vehiclePathfindResolvable_.insert(vehicleName);
    else vehiclePathfindResolvable_.erase(vehicleName);
}

void EngineState::registerVehiclePathfindRequest(const std::string& vehicleName, const std::string& targetName) {
    scriptedRequestDone_[{vehicleName, kScriptedRequestKindMoveOrPathfind}] = false; // 0x006f61b0 clears the done bit
    vehiclePathfindGoal_[vehicleName] = targetName;
}

bool EngineState::vehiclePathfindArrived(const WorldPosition& v, const WorldPosition& g, bool tight, bool mode2) {
    double dx = g.x - v.x, dz = g.z - v.z; // 0x00d9fc80: X/Z only, Y ignored
    double threshold = mode2 ? 2500.0 : (tight ? 4.0 : 16.0); // 0x011173a4 / 0x012a2f6c / 0x0127e214
    return dx * dx + dz * dz <= threshold;
}

double EngineState::vehiclePathfindCheckDoneCode(const std::string& vehicleName) {
    auto goalIt = vehiclePathfindGoal_.find(vehicleName);
    bool hasRequest = goalIt != vehiclePathfindGoal_.end();
    if (!vehiclePathfindResolvable_.count(vehicleName) && !hasRequest) return 2.0; // Sec9.10: unresolved vehicle/no slot-0 occupant fallback
    if (hasRequest) {
        auto reqIt = scriptedRequestDone_.find({vehicleName, kScriptedRequestKindMoveOrPathfind});
        if (reqIt == scriptedRequestDone_.end()) {
            vehiclePathfindGoal_.erase(goalIt); // cancelled/consumed: Sec9.10a "no ctx -> 2"
        } else if (!reqIt->second) {
            // Sec9.10a item 1 / port recipe 3+5: complete (done = 1 once) on ABORT
            // (goal/vehicle dead or missing) or ARRIVE; with no path/motion model and
            // no known positions, the recipe's step 5 immediate completion applies.
            const std::string goal = goalIt->second;
            bool complete = false;
            if (!hasVehicle(vehicleName) || !objectResolves().get(goal)) {
                complete = true; // ABORT: target does not resolve
            } else {
                const VehicleState& v = getOrCreateVehicle(vehicleName);
                const WorldPosition* gp = nullptr;
                if (hasVehicle(goal)) { const VehicleState& gv = getOrCreateVehicle(goal); if (gv.worldPosition.known()) gp = &gv.worldPosition.get(); }
                else if (hasCharacter(goal)) { const CharacterState& gc = getOrCreateCharacter(goal); if (gc.worldPosition.known()) gp = &gc.worldPosition.get(); }
                if (!v.worldPosition.known() || !gp) complete = true; // step 5: nothing to measure -> immediate completion
                else complete = vehiclePathfindArrived(v.worldPosition.get(), *gp);
            }
            if (complete) { reqIt->second = true; vehiclePathfindGoal_.erase(goalIt); }
        }
    }
    return static_cast<double>(scriptedRequestStatusCode(vehicleName, kScriptedRequestKindMoveOrPathfind));
}

int EngineState::tutorialAdvanceCount(int index) const {
    auto it = tutorialAdvanceCounts_.find(index);
    return it == tutorialAdvanceCounts_.end() ? 0 : it->second;
}

void EngineState::recordTutorialAdvance(int index) {
    ++tutorialAdvanceCounts_[index];
}

// tutorial_lock (Sec52.3, tranche 15, real-hit batch 2026-10-03) - see
// engine_state.h's own doc comment on tutorialLockCount()/
// recordTutorialLock() for the full citation.
// Design notes moved verbatim from EngineState::tutorialLockCount's declaration in engine_state.h
// (refactor step 3a):
// sfx_use_load_images (Sec49.4, `ui`) needs no state - CONFIRMED
// hardcoded true (0x0149365c set unconditionally at start-up,
// independently re-confirmed this tranche; already in Sec26.24's own
// screen-fade table) - the stub pushes a literal true directly, no
// accessor needed here.

// --- Ranking tranches 15/17/20/22/23 (spec-lua-api-behaviour.md
// Sec52/Sec51/Sec53/Sec55/Sec57), real-hit names only (2026-10-03,
// orchestrator-directed pass over the whole Sec50-Sec59 backlog: only
// names with a real, measured non-zero mission-drive call count get a
// real implementation; every other name in that backlog stays a
// generic logging stub - see the implementing commit's own message for
// the per-section tally). -----------------------------------------------

// tutorial_lock (Sec52.3, tranche 15). CONFIRMED: "it only accepts
// tutorial indices up to 188... the 21 entries from 189-209 can never
// be locked through this native at all." The resulting lock
// state/mechanism itself is not given beyond that bound (OPEN) - this
// project counts a "would lock" hit per in-range index rather than
// fabricate a state code into tutorialState()'s own stride-9 enum
// (whose "locked" value is nowhere confirmed in this spec). Reuses
// EngineState::tutorialLookup() (Sec10.4, above) for name resolution -
// the SAME 210-entry table every sibling in this family shares.
// Test-observable only.
int EngineState::tutorialLockCount(int index) const {
    auto it = tutorialLockCounts_.find(index);
    return it == tutorialLockCounts_.end() ? 0 : it->second;
}

void EngineState::recordTutorialLock(int index) {
    ++tutorialLockCounts_[index];
}

// dlc2_m02_clapboards_get/_reset (Sec55.5, tranche 22, real-hit batch
// 2026-10-03) - see engine_state.h's own doc comment for the full citation
// (CONFIRMED shape, HOST-SAFETY lower-bound deviation).
void EngineState::dlc2ClapboardsReset(int32_t rawCount) {
    // CONFIRMED: "clamped to at most 10, no lower clamp" - a negative
    // rawCount is stored as-is (matching the real native exactly; it is
    // harmless here because the zero-fill loop below only runs for a
    // strictly positive count).
    dlc2ClapboardCount_ = rawCount > 10 ? 10 : rawCount;
    for (int32_t i = 0; i < dlc2ClapboardCount_ && i < static_cast<int32_t>(dlc2ClapboardFlags_.size()); ++i) {
        dlc2ClapboardFlags_[static_cast<size_t>(i)] = false;
    }
}

int EngineState::dlc2ClapboardsGet(int32_t clapboardNumber) {
    // CONFIRMED (1-based -> 0-based): index = clapboardNumber - 1, compared
    // against dlc2ClapboardCount_ as a signed value with only the upper
    // bound checked by the real native. HOST-SAFETY DEVIATION (not a spec
    // fact): clapboardNumber <= 0 (the real arbitrary-read trigger) is
    // refused here instead of reproduced - this project does not simulate
    // crashes/arbitrary reads (group_get_next_npc / Sec28.5 precedent).
    if (clapboardNumber <= 0) {
        ++dlc2ClapboardsGetNegativeIndexGuardCount_; // HOST-SAFETY, see doc comment above
        return -1;
    }
    const int32_t index = clapboardNumber - 1;
    // CONFIRMED: out of the upper bound -> "no value at all" (-1 sentinel
    // here, pushed as nil by the stub, not false).
    if (index >= dlc2ClapboardCount_ || index >= static_cast<int32_t>(dlc2ClapboardFlags_.size())) return -1;
    return dlc2ClapboardFlags_[static_cast<size_t>(index)] ? 1 : 0;
}

EngineState::TutorialLookup EngineState::tutorialLookup(const std::string& name) {
    // 0x00717780: case-insensitive linear scan of the 210 names, first match
    // (Sec6.19/Sec26.28, CONFIRMED). ASCII case folding, as the C locale's
    // case-insensitive compare does.
    auto equalsNoCase = [](const char* a, const std::string& b) {
        size_t i = 0;
        for (; a[i] != '\0'; ++i) {
            if (i >= b.size()) return false;
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        }
        return i == b.size();
    };
    TutorialLookup out;
    for (int i = 0; i < kTutorialEntryCount; ++i) {
        const char* n = tutorialName(i);
        if (n && equalsNoCase(n, name)) {
            out.index = i;
            break;
        }
    }
    // Index 176's string is OPEN: "shorter than 4 characters or non-ASCII"
    // (Sec26.28). A name that could be it, with no known match before 176,
    // might resolve to 176 instead of `index`.
    bool candidate = name.size() < 4;
    for (unsigned char c : name) candidate = candidate || c >= 0x80;
    out.couldBeIndex176 = candidate && (out.index < 0 || out.index > 176);
    return out;
}

bool EngineState::coopIsActive() const {
    // 0x00867830 (Sec3.1, CONFIRMED - disassembly), conditions in order.
    if (!coopSession_.present.get()) return false;                                        // 1. no session
    if (!coopSession_.localIsHost.get() && !coopSession_.clientGate.get()) return false; // 2. host test first
    if (coopSession_.memberCount.get() < 2u) return false;                                // 3. unsigned, >= 2
    return coopSession_.otherMembersPassSlotCheck.get();                                  // 4. member walk
}

bool EngineState::coopLocalIsHost() const {
    // 0x008440a0 (Sec8.27): non-null session and +0x5c == +0x58.
    return coopSession_.present.get() && coopSession_.localIsHost.get();
}

bool EngineState::coopLocalIsClient() const {
    // 0x007bfbc0 (Sec10.2): null session -> false; else +0x5c != +0x58.
    return coopSession_.present.get() && !coopSession_.localIsHost.get();
}

// Design notes moved verbatim from EngineState::multiply33XorHashBucket's declaration in engine_state.h
// (refactor step 3a):
// 0x00dab330 (spec-texture-format.md Sec8.2's "multiply-33/XOR"
// hash: fold each character to lowercase, `hash = (hash*0x21) XOR c`,
// then `hash % bucketCount`) - CONFIRMED, empirically vector-validated
// (spec-lua-api-behaviour.md Sec5.3, 5/5 real I/O vectors matched
// exactly). Used by game_peg_load_with_cb (Sec8.24) via its own
// confirmed shared chain with game_peg_unload (Sec2.7),
// bucket_count=9000. A free function (no state needed) - exposed here
// rather than in a texture-domain header since this is the only
// consumer among this task's 12 in-scope names.
//
// Spec update 2026-09-30 (Sec8.2): initial value 0 and 32-bit wrap are
// now stated there, as implemented. Still OPEN there: signed vs
// unsigned modulo and the character load's signedness. This function
// takes the unsigned reading of both; it is the answer only when
// multiply33XorHashBucketUnambiguous() is true.
uint32_t EngineState::multiply33XorHashBucket(const std::string& name, uint32_t bucketCount) {
    // spec-texture-format.md Sec8.2, transcribed exactly: "fold each
    // character to lowercase, then hash = (hash * 0x21) XOR character, and
    // finally take hash % bucket_count." Empirically vector-validated,
    // spec-lua-api-behaviour.md Sec5.3 (5/5 real I/O vectors matched).
    uint32_t hash = 0;
    for (unsigned char c : name) {
        unsigned char lower = static_cast<unsigned char>(std::tolower(c));
        hash = (hash * 0x21u) ^ lower;
    }
    if (bucketCount == 0) return 0; // guard only - the real function is never called this pass with bucketCount==0 (game_peg_load_with_cb always passes the fixed literal 9000)
    return hash % bucketCount;
}

bool EngineState::multiply33XorHashBucketUnambiguous(const std::string& name, uint32_t bucketCount) {
    uint32_t hash = 0;
    for (unsigned char c : name) {
        if (c >= 0x80) return false;
        unsigned char lower = static_cast<unsigned char>(std::tolower(c));
        hash = (hash * 0x21u) ^ lower;
    }
    if (bucketCount != 0 && (bucketCount & (bucketCount - 1)) == 0) return true;
    return hash < 0x80000000u;
}

namespace {
std::string lowercased(const std::string& s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
} // namespace

std::string EngineState::zsceneTableKey(const std::string& name) {
    return lowercased(name);
}

// zscenePrep / zsceneIsLoaded and the per-frame zscene / cutscene driver
// live in src/lua_cutscene.cpp (Sec26.25, 2026-10-01 nnlt text).

bool EngineState::vintIsStdRes() const {
    // 0x00e1a150 (Sec26.26, CONFIRMED): first / second in double precision.
    const double first = static_cast<double>(vintRecordFirst_.get());
    const double second = static_cast<double>(vintRecordSecond_.get());
    double q;
    if (second != 0.0) {
        q = first / second;
    } else {
        // IEEE division by zero (the x87 default, exceptions masked):
        // +-infinity, or NaN for 0/0. Spelled out so no sanitizer sees a
        // division by zero.
        q = first == 0.0 ? std::numeric_limits<double>::quiet_NaN()
                         : (first > 0.0 ? std::numeric_limits<double>::infinity()
                                        : -std::numeric_limits<double>::infinity());
    }
    if (q < 1.5) return true; // the double at 0x012a2d30; false for NaN
    return vintDisplayMode_.get() == 2;
}

// =========================================================================
// pause-map stag mode / district control (spec-lua-api-behaviour.md Sec31)
// - batch 2026-10-02.
// =========================================================================

// Design notes moved verbatim from EngineState::pauseMapZoneControlFraction's declaration in engine_state.h
// (refactor step 3a):
// pause_map_stag_current_district_control's own CONFIRMED computation
// (Sec31.2): owned-weight / total-weight for the given zone, or 1.0 when
// total <= 0 (including NaN) - CONFIRMED, including the "a zone with no
// registered members" case (an unregistered zoneHandle, e.g. 0 on the
// no-selection path, naturally has total==0 here, matching Sec31.2's own
// "in practice over members with no zone, giving 1.0" reading exactly -
// no special-casing of zone 0 is needed, the general formula already
// produces it).
double EngineState::pauseMapZoneControlFraction(uint32_t zoneHandle) const {
    double total = 0.0;
    double owned = 0.0;
    auto it = zoneMembersByZone_.find(zoneHandle);
    if (it != zoneMembersByZone_.end()) {
        for (const auto& m : it->second) {
            total += static_cast<double>(m.weight);
            if (m.owned) owned += static_cast<double>(m.weight);
        }
    }
    // CONFIRMED (Sec31.2): total <= 0 (including a NaN total, which fails
    // every ordered comparison) pushes the constant 1.0 instead of dividing.
    if (!(total > 0.0)) return 1.0;
    return owned / total;
}

// pause_map_set_gps (Sec31.3). CONFIRMED: while stag mode (0x0229a317, the
// SAME global game_autosave's own gate reads - Sec27.12/Sec31.5) is on, a
// valid hovered zone (non-null, +0x44 non-null, +0x54 > 0) is copied into
// the selected-zone global and the Lua hook `pause_map_stag_completion`
// fires through the established hook dispatch trio (0x00e0cef0/0x00e0ca80/
// 0x00e0cd00). That real per-call dispatch already has its own general,
// existence-gated per-script firing pass elsewhere in this project
// (hook_registry.cpp's `pause_map_stag_completion` entry, Group 1) - rather
// than invoking the Lua callback a SECOND time through a separate path
// here, this method counts that the real gate fired
// (pauseMapStagCompletionHookFiredCount_), which is itself the real,
// CONFIRMED condition under which the engine's own dispatch would run.
// Outside stag mode, or with no valid hovered zone: CONFIRMED "it does
// nothing" to 0x0229a2ac - the real non-stag GPS-route behavior is not
// traced by this section and is not modeled (this project's own stated
// simplification, not a claim the real function does nothing at all).
void EngineState::pauseMapSetGpsStagBranch() {
    if (!autosave_.fourthFlagNonzero.get()) return; // OPEN until set; not in stag mode -> real GPS-route behavior, not modeled here
    const PauseMapHoveredZone& h = pauseMapHoveredZone_;
    if (h.zoneHandle != 0 && h.hasParentDistrict && h.fieldPlus0x54 > 0) {
        pauseMapSelectedZone_ = h.zoneHandle;
        ++pauseMapStagCompletionHookFiredCount_;
    }
}

// pause_map_stag_takeover_do_reward (Sec31.6). CONFIRMED: claims every
// member of the selected zone not yet owned (sets bit 0x02 of +0x3a - and,
// per Sec31.4, bit 0x01 too, though nothing in this batch's own scope reads
// that second bit, so only the Lua-visible "owned" bit is modeled); clears
// stag mode; requests an autosave (no longer suppressed, since clearing
// stag mode lifts game_autosave's own 0x0229a317 gate). The real reward-
// summary display (0x007ef9a0, HYPOTHESIS) and its own feeder reads (local
// player cash/respect, the parent district's control fraction via
// 0x0084ce80) have no further Lua-visible consequence among this batch's
// own in-scope names and are not modeled.
//
// Sec31.6's own text: "this function dereferences the selected zone's own
// +0x44 without a null check - it must only be called after a valid
// selection has been made." With no zone selected (pauseMapSelectedZone_ ==
// 0), the real engine's behavior is undefined (a null-pointer dereference);
// this host safely refuses instead of reproducing that crash, matching the
// project's own "flag a real hazard, don't reproduce it" precedent for the
// three crash-shaped paths ranking tranche 04 itself flags (Sec32.8).
// Design notes moved verbatim from EngineState::pauseMapStagTakeover's declaration in engine_state.h
// (refactor step 3a):
// pause_map_stag_takeover_do_reward's own CONFIRMED claim/clear-stag-
// mode/request-autosave mechanism (Sec31.6) - see this method's own .cpp
// doc comment for the full reasoning, including why this host refuses
// the real engine's own unchecked null-dereference safely instead of
// reproducing it (same §32.8 "flag, don't reproduce" precedent as the
// three crash paths below).
void EngineState::pauseMapStagTakeover() {
    if (pauseMapSelectedZone_ == 0) return; // real engine: undefined-behavior null-deref on +0x44; this host refuses safely instead
    auto it = zoneMembersByZone_.find(pauseMapSelectedZone_);
    if (it != zoneMembersByZone_.end()) {
        for (auto& m : it->second) {
            if (!m.owned) {
                m.owned = true;
                ++pauseMapTakeoverClaimedCount_;
            }
        }
    }
    autosave_.fourthFlagNonzero.set(false); // CONFIRMED: clears stag mode
    ++pauseMapTakeoverAutosaveRequestCount_; // CONFIRMED: requests an autosave; the real save itself (0x00b94ff0) is OPEN per spec, same precedent as game_autosave's own triggeredCount
}

// =========================================================================
// Ranking tranche 04 (spec-lua-api-behaviour.md Sec32) - batch 2026-10-02.
// =========================================================================

void EngineState::resetSpawnRegionMaxSpawnDist() {
    spawnRegionMaxSpawnDistSquared_ = std::numeric_limits<float>::max(); // CONFIRMED "reset" value, FLT_MAX
}

// set_time_of_day (Sec32.1 forward-jump arithmetic, corrected by Sec48.4's
// own real post-condition - see GameClock's own header doc comment).
// CONFIRMED: computes an hour delta forced non-negative (adds 24 if it
// would go negative - "the clock only ever advances"), combines it with
// the raw minute delta into a total seconds figure - kept as
// lastAdvanceSeconds, test-observable. The real engine then snaps the
// clock to the nearest time-of-day key (Sec48.4), which this project
// cannot compute (the key list is OPEN loaded data) - so hour/minute are
// forgotten rather than set to newHour/newMinute, honestly reflecting that
// the real post-call value is unknown, not fabricated as "whatever was
// requested."
void EngineState::setTimeOfDay(int64_t newHour, int64_t newMinute) {
    int64_t curHour = gameClock_.hour.get();     // OPEN until set (Sec48.2: known from construction in single player)
    int64_t curMinute = gameClock_.minute.get(); // OPEN until set
    int64_t hourDelta = newHour - curHour;
    if (hourDelta < 0) hourDelta += 24; // CONFIRMED: forced forward
    int64_t totalMinutes = hourDelta * 60 + (newMinute - curMinute);
    gameClock_.lastAdvanceSeconds = totalMinutes * 60;
    ++gameClock_.advanceRequestCount;
    gameClock_.second = 0; // CONFIRMED: the post-jump key-snap zeroes the seconds (Sec48.4)
    gameClock_.hour.forget();   // Sec48.4: the real post-snap value is OPEN, not newHour
    gameClock_.minute.forget(); // Sec48.4: the real post-snap value is OPEN, not newMinute
}

// Per-frame clock advance (Sec48.3, CONFIRMED mechanism - see GameClock's
// own header doc comment for the CHOSEN call cadence and the stated
// day-rollover simplification). A no-op while hour/minute are unknown
// (e.g. after setTimeOfDay forgot them above) - there is no known base to
// add a delta to.
void EngineState::gameClockAdvanceFrame(double realSeconds) {
    gameClock_.frameAdvanceSecondsAccumulated += realSeconds;
    if (!gameClock_.hour.known() || !gameClock_.minute.known()) return;
    int64_t secondsOfDay = gameClock_.hour.get() * 3600 + gameClock_.minute.get() * 60 + gameClock_.second;
    double gameSeconds = realSeconds * GameClock::kTimeScale;
    secondsOfDay = (secondsOfDay + static_cast<int64_t>(gameSeconds)) % 86400;
    if (secondsOfDay < 0) secondsOfDay += 86400; // defensive: realSeconds is never negative in scope
    gameClock_.hour.set(static_cast<int32_t>(secondsOfDay / 3600));
    gameClock_.minute.set(static_cast<int32_t>((secondsOfDay % 3600) / 60));
    gameClock_.second = static_cast<int32_t>(secondsOfDay % 60);
}

// satellite_weapon_mode_exit (Sec32.1). CONFIRMED: "with authority and the
// controller active, tears down camera/HUD/audio state locally"; without
// authority, forwards a request instead (this project's own no-networking-
// layer convention treats every call as locally authoritative, same as
// set_ignore_ai_flag/set_current_hit_points above - only the
// controller-active gate is modeled). The caller (lua_spec_confirmed_
// stubs.cpp's own stub_satellite_weapon_mode_exit) is where the 0x009df3d0
// correction is actually applied: `remote` is only ever passed true when
// playerRig().coopPlayerPresent is true.
void EngineState::satelliteWeaponExit(bool remote) {
    if (!satelliteWeapon_.active.get()) return; // OPEN until set
    if (remote) ++satelliteWeapon_.remoteExitCount;
    else ++satelliteWeapon_.localExitCount;
}

void EngineState::setQteSlotForTesting(int index, bool active, std::string owningPlayerName,
                                        std::vector<std::string> participants) {
    if (index < 0 || index > 1) return; // test/setup-only - the real record array is exactly 2 slots (Sec32.3)
    qteSlots_[index] = QteSlot{active, std::move(owningPlayerName), std::move(participants)};
}

// qte_human_is_used (Sec32.3). CONFIRMED: true if the queried character
// either IS the active slot's own owning player, or matches any of its
// participant id-pair fields; false otherwise (including no active slot at
// all). See QteSlot's own doc comment for this project's name-keyed
// simplification of the real local/remote selection mechanism.
bool EngineState::qteHumanIsUsed(const std::string& name) const {
    for (const auto& slot : qteSlots_) {
        if (!slot.active) continue;
        if (slot.owningPlayerName == name) return true;
        for (const auto& p : slot.participantNames) {
            if (p == name) return true;
        }
    }
    return false;
}

std::vector<EngineState::OpenSlotStatus> EngineState::openSlotInventory() const {
    std::vector<OpenSlotStatus> out;
    auto value = [&](const char* area, const auto& v) {
        out.push_back({area, v.global(), v.spec(), "value", v.known(), v.known() ? 1u : 0u});
    };
    auto map = [&](const char* area, const auto& m) {
        out.push_back({area, m.what(), m.spec(), "per-name map", false, m.knownCount()});
    };
    value("co-op", coopSession_.present);
    value("co-op", coopSession_.localIsHost);
    value("co-op", coopSession_.clientGate);
    value("co-op", coopSession_.memberCount);
    value("co-op", coopSession_.otherMembersPassSlotCheck);
    value("co-op", coopJoinType_);
    // Player runtime batch (2026-10-03): the one global OpenValue singleton
    // it adds (the per-character +0x1d99 bits live on CharacterState).
    value("player", playerControlsLockHandle_);
    map("tutorial", tutorialState_);
    value("vehicle-store", vehicleStoreActive_);
    map("zscene", zsceneLoadable_);
    value("zscene", zsceneSkipAllCutscenes_);
    value("zscene", zsceneCurrent_);
    value("zscene", zscenePending_);
    value("zscene", zsceneStateCode_);
    value("zscene", zsceneAutoSelectNearest_);
    value("zscene", zsceneRequeueOnReset_);
    map("zscene", zsceneHandleClass_);
    value("zscene", zsceneSoundtrackActive_);
    value("zscene", zsceneSoundtrackStartMs_);
    value("zscene", zsceneSoundtrackEnded_);
    value("zscene", zsceneNearestWorldObjectScene_);
    out.push_back({"zscene", "scene table 0x0153b294 (cutscene.xtbl names + <name>.cte_xtbl fields)",
                   "spec-lua-api-behaviour.md Sec26.25", "table", zsceneTableInstalled_, zsceneTable_.size()});
    value("cutscene", cutsceneManager_.present);
    value("cutscene", cutsceneManager_.sceneKey);
    value("cutscene", cutsceneManager_.field8);
    value("cutscene", cutsceneManager_.chainTarget);
    value("cutscene", cutscenePreloadMounted_);
    value("cutscene", cutsceneChainTarget_);
    value("cutscene", cutscenePlayerChecksPass_);
    value("cutscene", cutsceneLoadStamp_);
    value("cutscene", cutscenePlayingByte_);
    value("cutscene", cutsceneInProgressByte_);
    value("fade", screenFade_.state);
    value("fade", screenFade_.target);
    value("fade", screenFade_.flag);
    value("fade", screenFade_.documentLoaded);
    value("fade", screenFade_.logoAt);
    value("fade", screenFade_.holdLogoUntil);
    value("fade", screenFade_.imagesAt);
    value("fade", screenFade_.holdImagesUntil);
    value("fade", screenFade_.autoSaveStamp);
    value("fade", screenFade_.autoSaveCounter);
    value("fade", screenFade_.useLoadImages);
    value("fade", screenFade_.lastBroadcastWasOut);
    value("fade", screenFade_.modeStackTop);
    value("fade", screenFade_.cutsceneState);
    value("vint", vintRecordFirst_);
    value("vint", vintRecordSecond_);
    value("vint", vintDisplayMode_);
    value("vint", vintGlobalWidth_);
    value("vint", vintGlobalHeight_);
    value("vint", vintLayoutIndex_);
    value("vint", vintSafeFrameA_);
    value("vint", vintSafeFrameB_);
    value("vint", vintSafeFrameScale1_);
    value("vint", vintSafeFrameScale2_);
    value("other", hasLocalPlayer_);
    value("other", currentDefaultDocHandle_);
    map("other", objectResolves_);
    map("vint", vintTimeIndexByDoc_);
    size_t knownBits = 0;
    for (uint32_t m = missionFlagsWord_.knownMask(); m != 0; m &= m - 1) ++knownBits;
    out.push_back({"other", missionFlagsWord_.global(), missionFlagsWord_.spec(), "bit word",
                   missionFlagsWord_.knownMask() == 0xFFFFFFFFu, knownBits});

    // --- Batch 2026-10-01, spec-lua-api-behaviour.md Sec27/Sec28 ---
    value("batch2728", catMouseMinigame_.present);
    value("batch2728", catMouseMinigame_.fieldIc1IsOne);
    map("batch2728", missionComplete_);
    value("batch2728", completionScreen_.present);
    value("batch2728", completionScreen_.flagClusterSet);
    value("batch2728", completionScreen_.derefByteAtLeastOne);
    value("batch2728", completionScreen_.everyPlayerAtThreshold1);
    value("batch2728", completionScreen_.recordEnabled);
    value("batch2728", vcustCamera_.targetPresent);
    value("batch2728", vcustCamera_.targetValid);
    value("batch2728", vcustCamera_.targetAlive);
    map("batch2728", garagePreview_.vehicleTypeAtIndex);
    value("batch2728", garagePreview_.previewCache);
    map("batch2728", dialogForceClose_.slotMatchesId);
    map("batch2728", dialogForceClose_.timerArmedNotExpired);
    map("batch2728", dialogForceClose_.alreadyClosing);
    map("batch2728", dialogForceClose_.hasResultCallback);
    map("batch2728", dialogForceClose_.callbackNameBlank);
    map("batch2728", dialogForceClose_.fullyRemoved);
    value("batch2728", autosave_.suppressFlag);
    value("batch2728", autosave_.secondFlagSet);
    value("batch2728", autosave_.missionActive);
    value("batch2728", autosave_.thirdGateBlocks);
    value("batch2728", autosave_.fourthFlagNonzero);
    value("batch2728", playerSlots_.count);
    map("batch2728", playerSlots_.sendInviteOk);
    map("batch2728", playerSlots_.canSendInvite);
    map("batch2728", playerSlots_.joinFriendInProgressOk);
    value("batch2728", coopFriendlyFireRaw_);
    value("batch2728", steamInterfaceAvailable_);
    value("batch2728", inProgressType_.activeMissionPresent);
    value("batch2728", inProgressType_.activeMissionType);
    value("batch2728", inProgressType_.activityPresent);
    value("batch2728", inProgressType_.activityType);
    map("batch2728", helicopterFlyTo_.qualifies);
    map("batch2728", helicopterFlyTo_.applyGate);
    value("batch2728", guardianAngel_.modeIsThree);
    value("batch2728", guardianAngel_.objectPresent);
    // groupFirstNpcName()/groupNextNpcName() (Sec28.5/Sec28.6) no longer have
    // a separate OPEN-map backing field - they are derived from
    // groupMemberNames_, already inventoried via groupResolves_/
    // groupHasMembersBit_'s own Sec18.14 entries below.
    map("batch2728", humansInTriggerCount_);
    map("batch2728", vehicleInAirByCharacter_);
    value("batch2728", effectFinisher_.gamepadMode);
    map("batch2728", effectFinisher_.effectIndexValid);
    value("batch2728", effectFinisher_.successReturn);
    value("batch2728", playerRig_.localPlayer1Gender);
    value("batch2728", playerRig_.coopPlayerPresent);
    value("batch2728", playerRig_.coopPlayerGender);
    map("batch2728", stronghold_.stillLocked);
    map("batch2728", continuousExplosion_.definitionResolves);
    value("batch2728", continuousExplosion_.active);
    map("batch2728", characterGender_);
    value("batch2728", cellphoneAnimSuppressed_);
    value("batch2728", bossBattleMatt_.cheatSlot);
    value("batch2728", bossBattleMatt_.retryCounter);

    // --- Batch 2026-10-02, spec-lua-api-behaviour.md Sec31/Sec32 ---
    // (CharacterState's own per-character OpenValue fields - lifeState here,
    // like stateEnum/reviverHandle/maxHitPoints/currentHitPoints above -
    // are not centrally inventoried, matching this function's own existing
    // precedent of only listing EngineState-level slots.)
    value("batch3132", pauseMapTutorialMode_);
    value("batch3132", storeInterfaceActive_);
    value("batch3132", gameClock_.hour);
    value("batch3132", gameClock_.minute);
    value("batch3132", satelliteWeapon_.active);
    value("batch3132", helicopterFireDispatcherResult_);

    // --- Batch 2026-10-02, spec-lua-api-behaviour.md Sec38/Sec39/Sec40
    // ("ranking tranches 06/07/08") --- vcustPreview_.targetLive and
    // storePreviewGuards_.{gangPreviewAssetOrFallbackResolved,
    // galleryListObjectResolved} are CHOSEN plain bools (not OpenValue -
    // see their own doc comments), so they are not listed here, same
    // convention as every other CHOSEN plain-default field in this file
    // (e.g. pauseMenuSeenDisplayCalScreen_ above).
    value("ranking0608", saveSystemUi_.slotCount);
    map("ranking0608", pcuCategoryTable_.kindByIndex);
    map("ranking0608", pcuCatalogOutfits_.flagsByIndex);

    // --- Batch 2026-10-02 (resumed session), spec-lua-api-behaviour.md
    // Sec44/Sec45/Sec46 (ranking tranches 12-14). Per-character/per-vehicle
    // OpenValue fields (CharacterState::hiddenFlag, VehicleState::
    // tireDurability/tireDamageMultiplier/forceFlags1d7a/lightsForceFlags/
    // tireIndicatorObjectDisabled/seat0Occupied, CharacterState::
    // forceFlags1c98/flagsE4, etc.) are NOT centrally inventoried here,
    // matching this function's own existing precedent (see the
    // "batch3132" comment above) of only listing EngineState-level slots.
    value("batch444546", ambientGangSpawnEnabled_);
    value("batch444546", cellCameraEnabled_);
    value("batch444546", ambientCopSpawnEnabled_);
    value("batch444546", actionNodesShouldntFlee_);
    value("batch444546", actionNodesRestrictSpawning_);

    // --- Batch 2026-10-03, spec-lua-api-behaviour.md Sec49 ("ranking
    // tranche 16") --- shopPurchase_.triggerResolves and
    // strongholdPurchaseUpgrade_.strongholdResolves are CHOSEN plain bools
    // (not OpenValue - see their own doc comments), so they are not listed
    // here, same convention as vcustPreview_.targetLive above.
    map("ranking16", spawnOverride_.categoryNameResolves);

    // --- Batch 2026-10-08 (Batch 7, dedicated agent), spec-lua-api-
    // behaviour.md Sec30 ranking tranche 03, the 16 names not already
    // wired by b03. Per-character/per-vehicle OpenValue fields
    // (CharacterState::combatMoveSlot1358/caeAdvanceDestinationComputes/
    // turnRefusalPredicateFires/turnRequestBusy/turnStartRoutineSucceeds
    // Immediately, VehicleState::teleportNameGateField0xbf8a8) are NOT
    // centrally inventoried here, matching this function's own existing
    // precedent above. Only EngineState-level/BossBattleMatt-level slots
    // are listed, same as bossBattleMatt_.cheatSlot/retryCounter above.
    value("tranche03batch7", pathfindResult_);
    value("tranche03batch7", rcSignalRange_);
    value("tranche03batch7", bossBattleMatt_.npcBahamutResolves);
    value("tranche03batch7", bossBattleMatt_.avatarModelResolves);
    value("tranche03batch7", bossBattleMatt_.styleTestPcResolves);

    // --- Batch 3 (2026-10-08), spec-lua-api-behaviour.md Sec11+Sec12 - only
    // EngineState-level (singleton) slots, same "per-character/per-vehicle
    // fields are NOT centrally inventoried" precedent as the batch3132
    // comment above. ---
    value("batch3_pcr_vehicle", pcrPlasticSurgeonMode_);
    map("batch3_pcr_vehicle", pcrMorph_.sessionResolves);
    map("batch3_pcr_vehicle", pcrMorph_.slotResolves);
    value("batch3_pcr_vehicle", pcuStreaming_.rowCounterPending);
    value("batch3_pcr_vehicle", pcuStreaming_.listMembershipPending);
    value("batch3_pcr_vehicle", hordeModeActive_);
    map("batch3_pcr_vehicle", storeCameraPreset_.presetResolves);
    value("batch3_pcr_vehicle", pauseMenuOptions_.acceptGatePasses);
    value("batch3_pcr_vehicle", vintRemapStaging_.dialogAlreadyOpen);

    // Wave 2 batch W2-1 (2026-10-08, lua_spec_confirmed_stubs_b15_wave2_
    // core_dispatchers.cpp): the two EngineState-level OPEN singletons it
    // adds. Its per-character OpenValue fields (CharacterState::
    // vehicleEnterSubmitAccepted/...Forced/vehicleEnterAiOrderStarts/
    // stateCodeByte501/504/enterCheckHelper9b95c0/moveToPredicateA3bae*/
    // moveToMode3AnimTagResolves/moveToDispatchSucceeds/
    // damageImmuneDescriptorBit33) follow the same "not centrally
    // inventoried" precedent as Batch 7's above.
    value("wave2w21", moveToActionIdCounter_);
    value("wave2w21", vehicleNavmeshDispatchResult_);
    return out;
}

// ===========================================================================
// Batch 2026-10-08 (Batch 7, dedicated agent) - spec-lua-api-behaviour.md
// Sec30 ("ranking tranche 03"), the shared engine-state mechanisms backing
// the 16 of its 25 names NOT already wired by
// lua_spec_confirmed_stubs_b03_vintui_r03_r05.cpp's own 9 (see
// src/lua_spec_confirmed_stubs_b14_batch7_tranche03.cpp for the stub bodies
// themselves). Two genuinely new shared mechanisms, each consumed by more
// than one of this batch's own natives, kept here rather than duplicated
// per call site:
//   1. The 70-entry CAE combat-action table (0x012e1c60, Sec30.4) - a
//      plain, file-backed name->index roster this project reproduces
//      VERBATIM from the spec's own text, used by ai_suggest_action/
//      ai_do_scripted_advance.
//   2. The shared script-request-id allocator (0x006f62b0, Sec30.5's own
//      preamble) - used by ai_suggest_action (channel 3), turn_to_do
//      (channel 0), and vehicle_pathfind_to_do (channel 2).
// Plus vehicle_stop_do/teleport_vehicle_to_object's own shared
// EngineState::stopVehicle() (0x00abe510, Sec30.5/Sec30.6).
// ===========================================================================

namespace {

// The 70 CAE action names, in index order, verbatim from spec-lua-api-
// behaviour.md Sec30.4's own "All 70 names, read from the file image" text.
const char* const kCaeActionNames[70] = {
    "fire", "fire sweep", "fire suppress", "fire chaos", "throw grenade",
    "throw weapon", "melee", "melee vehicle", "cover take def", "cover take off",
    "cover advance", "cover stay down", "cover fire", "cover popout", "hold position",
    "melee stand back", "move retreat", "move regroup", "move advance", "move chase",
    "avatar chase", "move surround", "move rush", "move rush to melee", "move scripted",
    "move to navmesh", "move sidestep", "move to post combat", "move to post idle", "move close in",
    "follow make way", "follow avoid LOF", "follow formation", "follow acquire", "follow player",
    "follow teleport", "follow lemming", "follow to car", "follow other car", "follow carjack",
    "follow browse", "investigate move", "investigate look", "investigate peek", "reload",
    "pickup weapon", "human shield grab", "taunt", "vehicle passenger", "vehicle enter scpt",
    "vehicle extract", "brute throw prop", "brute car flip", "brute bull rush", "brute qte player",
    "brute attack brute", "brute gun finisher", "avatar shockwave", "avatar stomp", "avatar taunt",
    "avatar fireballs", "avatar teleport stomp", "avatar bullrush", "quick kill", "zombie eat",
    "zombie explode", "killbane chase", "killbane strafe", "roller blader change", "idle",
};

// Portable ASCII case-insensitive compare - same convention as
// lua_spec_confirmed_stubs_common.h's caseInsensitiveEquals, duplicated here
// (rather than included) to avoid a stub-layer header dependency from this
// engine-state translation unit.
bool CaeNameEqualsCaseInsensitive(const std::string& a, const char* b) {
    size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

} // namespace

int32_t EngineState::lookupCaeActionIndex(const std::string& name) const {
    for (int32_t i = 0; i < 70; ++i) {
        if (CaeNameEqualsCaseInsensitive(name, kCaeActionNames[i])) return i;
    }
    // CONFIRMED sentinel 71 (0x47) - also where the literal "NONE" sentinel
    // lands (Sec30.4: "'NONE' -> index 71, silent no-op" is the SAME value
    // 0x004f2610's own "not found" path returns, so no separate special
    // case is needed for it here).
    return kCaeActionNotFound;
}

// Design notes moved verbatim from EngineState::allocateScriptRequestId's declaration in engine_state.h
// (refactor step 3a):
// --- Shared script-request-id allocator 0x006f62b0 (Sec30.5's own
// preamble: "cancels any tracked request for the same object+channel,
// allocates a 12-bit sequence id OR-ed with a machine-index nibble
// (HYPOTHESIS); channel 0 = turn_to_do, 2 = vehicle_pathfind_to_do, 3 =
// the AI action writers"). This project is single-player/host-only
// everywhere (stated, project-wide simplification - see
// replicateStateChange's own doc comment) so the machine-index nibble
// is always 0 here; only the 12-bit sequence counter is modelled,
// wrapping at 0xFFF (CONFIRMED structure). A fresh allocate for the
// SAME (object, channel) pair simply overwrites whatever was tracked
// before (CONFIRMED "cancels any tracked request"). -----------------
int32_t EngineState::allocateScriptRequestId(const std::string& objectName, int channel) {
    auto key = std::make_pair(objectName, channel);
    int32_t id = nextScriptRequestSeq_++;
    if (nextScriptRequestSeq_ > 0xFFF) nextScriptRequestSeq_ = 1; // 12-bit sequence, CONFIRMED structure (Sec30.5)
    // "OR-ed with a machine-index nibble (HYPOTHESIS)" - always 0 here
    // (this project is single-player/host-only everywhere).
    activeScriptRequestId_[key] = id; // CONFIRMED: "cancels any tracked request for the same object+channel"
    return id;
}

void EngineState::releaseScriptRequestId(const std::string& objectName, int channel) {
    activeScriptRequestId_.erase(std::make_pair(objectName, channel));
}

// Design notes moved verbatim from EngineState::stopVehicle's declaration in engine_state.h
// (refactor step 3a):
// --- vehicle_stop_do / teleport_vehicle_to_object's own SHARED
// 0x00abe510 stop routine (Sec30.5: "vehicle_stop_do... 0x00abe510(1)
// ...; 0x00abe510 (shared with teleport_vehicle_to_object)..."). Only
// the CONFIRMED gate/branch structure is modelled (direct-apply vs.
// replicate-only, same double-/single-gate family every other setter
// in this header already resolves true-in-single-player) - the real
// physics stop (0x00b46ca0/0x00a90c70) and the AI-driving-controller
// stop on the +0x808 sub-object are NOT simulated (no physics/AI-
// subobject model in this project). `authorityPasses` is computed by
// the caller from whichever real gate applies to ITS own path
// (character's controlLockGatePasses for the via-character path,
// vehicle's own forceFlagGatePasses for the direct/vehicle-name path -
// Sec30.5: "authority checked on the character (if via character) or
// the vehicle (if direct)").
void EngineState::stopVehicle(const std::string& vehicleName, bool authorityPasses) {
    VehicleState& v = getOrCreateVehicle(vehicleName);
    if (authorityPasses) {
        ++v.stopRequestCount; // CONFIRMED direct-apply branch (0x00abe510, 1)
    } else {
        replicateStateChange("vehicle_stop", vehicleName); // CONFIRMED: opcode-0x43 stop request to whoever owns the vehicle
    }
}

// =============================================================================
// Wave 2, Batch W2-7 (2026-10-08): spec-lua-api-behaviour.md Sec38-60 - see
// engine_state.h's own doc comments on each of these methods (and on the
// fields they touch) for the full per-native citation/reasoning this file
// keeps terse. src/lua_spec_confirmed_stubs_b26_wave2_vehicles_helicopter.cpp
// and src/lua_spec_confirmed_stubs_b27_wave2_spawn_world_audio.cpp are the
// stub bodies that call these.
// =============================================================================

// roadblock_create / roadblock_destroy (Sec39.2). CONFIRMED: "-1 on any
// failure." Placement succeeds only when roadblockPlacementResolves_ is
// known true for placementObjectName (this project has no road-network/
// geometry data to derive that from; a test sets it, an un-set-up real
// call takes the honest "fails" branch rather than throwing mid-creation -
// creation itself is a CONFIRMED-shape operation that must still return a
// real sentinel on failure, not an engine-state refusal).
int64_t EngineState::createRoadblock(const std::string& templateName, const std::string& placementObjectName) {
    bool placementOk = roadblockPlacementResolves_.known(placementObjectName) && roadblockPlacementResolves_.get(placementObjectName);
    if (!placementOk) return -1;
    int64_t handle = nextRoadblockHandle_++;
    roadblocks_[handle] = RoadblockRecord{templateName, placementObjectName};
    return handle;
}

bool EngineState::destroyRoadblock(int64_t handle) {
    auto it = roadblocks_.find(handle);
    if (it == roadblocks_.end()) return false; // CONFIRMED: "requires the roadblock-kind descriptor bit" - treated as absent otherwise
    roadblocks_.erase(it);
    return true;
}

// guardian_angel_start_zone_pinning / _clear_zone_pinning (Sec53.3).
// CONFIRMED: the release runs UNCONDITIONALLY before the name lookup, so
// an empty zoneSetName (the _clear_ entry point) and a failed/oversized
// zoneSetName both leave currentZonePinSetName cleared - the difference is
// only whether a NEW name is then recorded (and whether that new pin
// actually "took," per zonePinSucceeded).
void EngineState::guardianAngelStartZonePinning(const std::string& zoneSetName) {
    guardianAngel_.currentZonePinSetName.clear(); // CONFIRMED: unconditional release first
    guardianAngel_.zonePinSucceeded = false;
    if (zoneSetName.empty()) return; // the _clear_ entry point: release only, nothing new to pin
    guardianAngel_.currentZonePinSetName = zoneSetName; // CONFIRMED: recorded as "current" even when it fails below
    if (guardianAngel_.zoneSetSize.known(zoneSetName) && guardianAngel_.zoneSetSize.get(zoneSetName) <= 8) {
        guardianAngel_.zonePinSucceeded = true;
    }
    // CONFIRMED: ">8 zones pins nothing at all" (zonePinSucceeded stays
    // false) - also the honest default when the zone-set's own size was
    // never registered (no real zone-set data table in this project).
}

// dlc2_m02_clapboards_set (Sec55.5 MEMSAFE) - hardened as ONE unit with the
// already-registered dlc2ClapboardsGet/_Reset above (same 10-byte array,
// same dlc2ClapboardCount_ field). SAME signed-index bounding scheme as
// dlc2ClapboardsGet: argument <= 0 is HOST-SAFETY-refused (own named
// counter, per this batch's hardening policy of one counter per hardened
// site); the upper bound is the CONFIRMED real "no-op past the end" shape -
// _set has no return value to shape, so an out-of-range write is simply
// skipped (the CONFIRMED real native would write out-of-bounds here; this
// host instead silently refuses, matching the common in-range case's own
// observable result of "the array holds what you asked" while never
// touching memory outside the 10-byte array).
// Design notes moved verbatim from EngineState::dlc2ClapboardsSet's declaration in engine_state.h
// (refactor step 3a):
// --- guardian_angel_start_zone_pinning / _clear_zone_pinning /
// _release_attack_groups (Sec53.3): see GuardianAngel::
// currentZonePinSetName's own doc comment above (extended in place,
// this batch's own additions to that already-existing struct).

// --- helicopter_* cluster (Sec53.2): see VehicleState's own new
// fields above (largeDspiral/dontMoveInCombat/dontExplodeInAir/
// maxBankAngleOverrideEnabled/landDistanceGatePasses/landed/
// dropoffOccupancyGatePasses/dropoffRequested) - "accepts any resolved
// vehicle, not just a helicopter," so these live on VehicleState, not
// a separate per-helicopter map.

// --- dlc2_m02_clapboards_set (Sec55.5) - MEMSAFE, hardened AS ONE UNIT
// WITH its already-registered siblings dlc2ClapboardsGet/_Reset
// (wave-1 Batch 5, lua_spec_confirmed_stubs_b05_invuln_r15_23.cpp /
// engine_state.h's own existing dlc2ClapboardCount_/dlc2ClapboardFlags_
// /dlc2ClapboardsGetNegativeIndexGuardCount_ fields above) - SAME
// signed-index bounding scheme: argument <= 0 is HOST-SAFETY-refused
// (own named counter, NOT dlc2ClapboardsGetNegativeIndexGuardCount_ -
// "count every hardened-path firing via a NAMED PER-SITE counter" per
// this batch's own hardening policy); the upper bound (index >= count
// or >= the 10-byte array) is the CONFIRMED real behaviour already
// established by _get (there: "no value at all"; here: the write is
// simply skipped, there is no return value to shape). See
// EngineState::dlc2ClapboardsGet's own doc comment (above) for the
// full CONFIRMED shape this reuses.
void EngineState::dlc2ClapboardsSet(int32_t clapboardNumber, bool value) {
    if (clapboardNumber <= 0) {
        ++dlc2ClapboardsSetNegativeIndexGuardCount_; // HOST-SAFETY, see doc comment above
        return;
    }
    const int32_t index = clapboardNumber - 1;
    if (index >= dlc2ClapboardCount_ || index >= static_cast<int32_t>(dlc2ClapboardFlags_.size())) return; // CONFIRMED upper-bound shape, no-op
    dlc2ClapboardFlags_[static_cast<size_t>(index)] = value;
}

// world_despawn_all_vehicles / world_despawn_all_pedestrians (Sec46.3).
// Design notes moved verbatim from EngineState::despawnAllVehicles's declaration in engine_state.h
// (refactor step 3a):
// --- world_despawn_all_vehicles / world_despawn_all_pedestrians
// (Sec46.3) ---------------------------------------------------------
// Walks vehicles()/characters() (this project's own stand-in for "the
// dedicated ambient/traffic lists," same convention VehicleState's own
// doc comment on vehicles() already establishes) and marks every
// non-protected, non-player-occupied entry despawnedPendingDelete -
// "deferring actual deletion through the engine's normal pending-
// delete queue rather than freeing synchronously" is modelled as
// "marked, not erased from the map" (consistent with this project's
// general no-real-deletion convention elsewhere, e.g. garage/pool
// records). Returns the count actually despawned.
uint64_t EngineState::despawnAllVehicles() {
    uint64_t count = 0;
    for (auto& entry : vehicles_) {
        VehicleState& v = entry.second;
        if (v.despawnPlayerOccupied || v.despawnProtected || v.despawnedPendingDelete) continue;
        v.despawnedPendingDelete = true;
        ++count;
    }
    return count;
}

uint64_t EngineState::despawnAllPedestrians() {
    uint64_t count = 0;
    for (auto& entry : characters_) {
        CharacterState& c = entry.second;
        if (c.despawnProtected || c.despawnedPendingDelete) continue;
        c.despawnedPendingDelete = true;
        ++count;
    }
    return count;
}

// Wave 2 batch W2-6 (2026-10-08) - the machinima family's shared path-
// builder stand-in (spec-lua-api-behaviour.md Sec44.4/Sec56.5): the real
// builder has 3 failure exits (process-token open, folder lookup, folder
// creation) that all leave the output buffer unwritten, and NO caller
// checks this - a confirmed crash/data-loss shape (`game_machinima_delete_
// clip` can delete the wrong file from uninitialized stack content). This
// project's own hardened divergence, applied project-wide to the whole
// family per the orchestrator's standing policy: fail closed instead -
// every caller below checks `ok` before touching `path`, so the
// uninitialized-buffer shape cannot reach any of them. `clipName` itself
// stands in for the real UI-encoded/plain-name distinction (Sec56.7) -
// this project has no companion encoder, so every name is treated as the
// real engine's own "plain (non-encoded) input" case, which is exactly
// the case the hardening must cover.
EngineState::MachinimaPathResult EngineState::buildMachinimaClipPath(const std::string& clipName) {
    MachinimaPathResult result;
    if (clipName.empty()) {
        ++machinima_.pathBuilderFailureGuardCount; // CHOSEN: an empty name never resolves to a real path
        return result;
    }
    if (!machinimaPathBuilderSucceeds().get()) { // OPEN until set; test controls the "environment failure" scenario
        ++machinima_.pathBuilderFailureGuardCount;
        return result;
    }
    result.ok = true;
    result.path = "clips/" + clipName + ".srtt_clip"; // CHOSEN stand-in path, never dereferenced as a real filesystem path
    return result;
}

bool EngineState::registerTriggerGeometryFromZoneData(const std::string& triggerName, std::string* note) {
    const sr3zone::ObjectRecord* rec = resolveZoneObject(triggerName);
    std::optional<sr3zone::Position3f> pos = rec ? sr3zone::ObjectPosition(*rec) : std::nullopt;
    if (!pos) return false;
    TriggerVolumeGeometry g;
    g.px = pos->x; g.py = pos->y; g.pz = pos->z;
    g.positionKnown = true;

    std::optional<std::string> shapeTypeText = sr3zone::TriggerShapeType(*rec);
    std::optional<int> shapeCode = shapeTypeText ? sr3zone::TriggerShapeCodeForTypeString(*shapeTypeText)
                                                  : std::nullopt;
    const sr3zone::ObjectPropertyEntry* param1 = sr3zone::TriggerShapeParam1(*rec);
    const sr3zone::ObjectPropertyEntry* param2 = sr3zone::TriggerShapeParam2(*rec);
    bool decoded = false;
    if (shapeCode && *shapeCode == 0 && param1 && param2) {
        std::optional<sr3zone::Position3f> vmin = sr3zone::DecodePropertyAsVec3(*param1);
        std::optional<sr3zone::Position3f> vmax = sr3zone::DecodePropertyAsVec3(*param2);
        if (vmin && vmax) {
            g.shapeCode = 0;
            g.boxMinX = vmin->x; g.boxMinY = vmin->y; g.boxMinZ = vmin->z;
            g.boxMaxX = vmax->x; g.boxMaxY = vmax->y; g.boxMaxZ = vmax->z;
            decoded = true;
        }
    } else if (shapeCode && *shapeCode == 1 && param1) {
        std::optional<float> radius = sr3zone::DecodePropertyAsFloat(*param1);
        if (radius) {
            g.shapeCode = 1;
            g.radius = *radius;
            decoded = true;
        }
    } else if (shapeCode && *shapeCode == 2 && param1 && param2) {
        // f752324 spec answer: +0x90 (param1) = axis LENGTH, +0x94 (param2) = RADIUS.
        std::optional<float> length = sr3zone::DecodePropertyAsFloat(*param1);
        std::optional<float> radius = sr3zone::DecodePropertyAsFloat(*param2);
        if (radius && length) {
            g.shapeCode = 2;
            g.radius = *radius;
            g.length = *length;
            g.ex = 0; g.ey = *length; g.ez = 0; // CHOSEN axis (straight up) - OPEN source, Sec1.14
            decoded = true;
        }
    }
    g.shapeKnown = decoded;
    if (!decoded) {
        g.shapeCode = 1; // CHOSEN fallback: sphere radius 3.0
        g.radius = 3.0;
    }
    registerTriggerGeometry(triggerName, g);
    noteTriggerRegistrationOrder(triggerName);
    if (note) {
        char buf[512];
        if (decoded) {
            std::snprintf(buf, sizeof buf,
                          "in-world trigger '%s' geometry registered - REAL position (%.2f %.2f %.2f) and REAL "
                          "shape '%s' (enum %d) from zone data",
                          triggerName.c_str(), g.px, g.py, g.pz, shapeTypeText->c_str(), *shapeCode);
        } else {
            const std::string reason = shapeTypeText ? ("unmapped string \"" + *shapeTypeText + "\"")
                                                      : std::string("no trigger_shape_type property");
            std::snprintf(buf, sizeof buf,
                          "in-world trigger '%s' geometry registered - real position (%.2f %.2f %.2f) from zone "
                          "data, CHOSEN sphere radius=%.1f fallback (shape not decodable: %s)",
                          triggerName.c_str(), g.px, g.py, g.pz, g.radius, reason.c_str());
        }
        *note = buf;
    }
    return true;
}

// ce4556f Sec29 (m19 chem_0N_brute): a placed Script_Npc whose own "<name>_group"
// script_group exists in the same zone is a group member; it becomes a live,
// resolvable character only once that group is created (group_create). Until
// then its lower-case script alias resolves to FALSE (not OPEN).
void EngineState::markGroupCreated(const std::string& name) {
    groupsCreated_.insert(name);
    auto it = pendingGroupMembers_.find(AsciiLowerFold(name));
    if (it == pendingGroupMembers_.end()) return;
    for (const std::string& alias : it->second) objectResolves_.set(alias, true);
    pendingGroupMembers_.erase(it);
}

} // namespace sr3luahost
