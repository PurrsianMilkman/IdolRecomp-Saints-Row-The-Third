// EngineState method definitions moved verbatim out of include/sr3luahost/engine_state.h
// (structural refactor, step 1b: no behaviour change). Area: progression.

#include "sr3luahost/engine_state.h"

namespace sr3luahost {

bool EngineState::missionIsComplete(const std::string& name) const {
        return missionComplete_.known(name) ? missionComplete_.get(name) : false;
    }

// Design notes moved verbatim from EngineState::tutorialDescriptorDefaultParam20's declaration in engine_state.h
// (refactor step 3a):
// audio_any_conversation_playing (Sec33.4): CONFIRMED mechanism - loops
// every session member's own slot on a named per-session channel
// ("mission_conv" here; the spec's own text notes this is "a generic,
// reusable mechanism ... one such channel among possibly several").
// This project tracks no per-member array anywhere (CoopSession above
// is a handful of summary predicates, not a member list) - collapsed
// to one "is anyone active on this channel" flag per channel name, a
// stated simplification; OPEN until a test sets it.
int32_t EngineState::tutorialDescriptorDefaultParam20(int32_t index) {
        // Spec sync e6691f1 (Sec6.19 FINAL): 203 descriptors hold 2; indices 188 and 196-201 hold 1.
        return (index == 188 || (index >= 196 && index <= 201)) ? 1 : 2;
    }

bool EngineState::missionConvActive() const {
        return conversationChannelActive_.known("mission_conv") ? conversationChannelActive_.get("mission_conv") : false;
    }

const std::unordered_set<std::string>& EngineState::grantNewGameAutoUnlocks(
        const std::vector<sr3tables_progression::Unlockable>& unlockables) {
        for (const auto& u : unlockables) {
            if (u.autoUnlock != sr3tables_progression::UnlockableAutoUnlock::None) {
                newGamePurchasedUnlocks_.insert(u.name);
            }
        }
        return newGamePurchasedUnlocks_;
    }

// Design notes moved verbatim from EngineState::setMissionPhase's declaration in engine_state.h
// (refactor step 3a):
// The general-purpose phase setter, `0x006d86f0(newPhase)` (Sec21.27a).
// CONFIRMED real behaviour: phase 1 is "never actually observable as a
// steady value" - its own case-1 handler's 4-entry jump table
// immediately self-loops again INSIDE the same call, keyed by the
// bound mission's own type: type 2 unconditionally, type 1
// (Sec21.27a's own text gives no detail on the condition gating type
// 1's self-loop - CHOSEN to treat it identically to type 2 here, since
// nothing distinguishes them further and this project's own default
// mission type is 0, never 1, unless a test explicitly overrides it)
// -> phase 3; types 0 and 3 -> phase 5, which (Sec21.27a's own phase-5
// table row) ALSO arms the mission-start marker at that point for
// those two types.
void EngineState::setMissionPhase(int32_t newPhase) {
        missionContext_.phase = newPhase;
        ++missionContext_.phaseTransitionCount;
        if (newPhase == 1) {
            if (missionContext_.boundMissionType == 1 || missionContext_.boundMissionType == 2) {
                missionContext_.phase = 3;
            } else {
                missionContext_.phase = 5;
                missionContext_.missionStartMarkerArmed = true;
            }
            ++missionContext_.phaseTransitionCount;
        }
    }

// Design notes moved verbatim from EngineState::bindMissionPhaseMachine's declaration in engine_state.h
// (refactor step 3a):
// `0x006d6920`, "bind mission" (Sec21.27a) - the ONLY real writer of
// phase 8, and this project's own one hook for the entire 8 -> (2,
// fresh+named only) -> 1 -> (3 or 5) sequence Sec21.27a's own text says
// resolves "within a few frames" of real engine time - collapsed into
// one synchronous call, the SAME simplification this project's own
// prior beginMissionPhaseTracking() already applied to the boolean-only
// facade (see that method's own doc comment, below, for the full root-
// cause history this collapsing resolves): CONFIRMED to be the one
// honestly-reachable sequence a host that only ever resumes a mission's
// own `_start` once it's safe to do so (this project's own
// kMissionStartIsRestart=false fresh-start convention,
// tools/lua_host_run.cpp) could ever observe.
//
// `isRestart`/a non-empty `name` select whether phase 2's own marker-arm
// branch runs at all (Sec21.27a's phase-2 table row: "fires only when
// the bound mission is NOT a restart and has a name... otherwise...
// skips phase 2 entirely and relaunches script_mission_start directly"
// - this project's host always does a fresh, named start, so phase 2
// fires on every real call today; the restart branch exists for
// completeness/future test coverage, not because this project
// currently drives it).
//
// After settling at phase 3 or 5, if `name` is flagged silentClass (see
// missionSilentClass() below) this ALSO fires the organic "silent"-
// class auto-success trigger (`0x006db7fa`, Sec21.27b's own organic-
// trigger table: "Success, unconditionally") - the CHOSEN best-effort
// organic SUCCESS path this task wires; see missionSilentClass()'s own
// doc comment for why it is a documented no-op for every one of the
// real 49 probed missions today (no population table for which of them
// carries class bit 0x20).
//
// FIX 2026-10-08 (mission-context-name task): ALSO sets
// MissionContext::currentMissionName/currentMissionEnding here, at this
// SAME real bind call site (0x006d6920 - the identical function
// Sec21.27a already cites as the one real writer of 0x14c8460's first
// dword, which this field stands in for), not a second, independent
// write path. Closes the second-order blocker Sec1.5's own identity gate
// (mission_end_failure, shared verbatim by mission_end_success/Sec6.12
// and mission_end_to_activity/Sec6.20) hit once the 12-mission phase-8
// blocker above was fixed: those three natives were already wired to
// read these two fields correctly, but nothing ever set them, so every
// real mission reaching one of them threw OpenStateError instead of
// getting a real (gate-failing or gate-passing) answer. See each
// field's own doc comment (above) for the full citation.
void EngineState::bindMissionPhaseMachine(const std::string& name, int32_t type, bool isRestart) {
        missionContext_.missionBound = true;
        missionContext_.boundMissionName = name;
        missionContext_.boundMissionType = type;
        missionContext_.currentMissionName.set(name);
        missionContext_.currentMissionEnding.set(false); // freshly bound: alive, not yet tearing down - see field's own doc comment
        setMissionPhase(8);
        if (!isRestart && !name.empty()) {
            setMissionPhase(2); // 0x006D9310's own direct write
            missionContext_.missionStartMarkerArmed = true; // tail-call into 0x006d5720, the real marker-arm function
        }
        setMissionPhase(1); // self-loops to 3 or 5 inside setMissionPhase() above
        if (!name.empty() && missionSilentClass_.known(name) && missionSilentClass_.get(name)) {
            setMissionPhase(4);
            if (missionEndDispatch(name, /*successFlag=*/true)) ++missionContext_.missionEndSilentAutoSuccessCount;
        }
    }

// Design notes moved verbatim from EngineState::missionEndDispatch's declaration in engine_state.h
// (refactor step 3a):
// `0x006d9f70`, the end-of-mission super-dispatcher (Sec21.27b). Entry
// gates (Sec21.27b's own text, CONFIRMED): refuses if no mission is
// bound, if phase is already 8 (mid-bind), or if `name` doesn't match
// the currently-bound mission (this project's "key by name string"
// stand-in for real pointer identity, same convention as
// MissionContext::currentMissionName above). Implements the 4-way TYPE
// switch structurally:
//  - type 1: fires the completion-flag writer AND mission_unlock's own
//    setter with literal 1 (Sec21.27b: "this independently confirms...
//    mission type 1 is a silent/no-completion-UI variant"). CHOSEN:
//    gated on successFlag - Sec21.27b's own text flags "whether
//    0x006d9f70's type-1/type-2 handlers further gate this call on
//    successFlag internally" as "one honest loose end, not re-chased";
//    gating conservatively on successFlag here never marks a FAILED
//    mission complete through this path, matching every OTHER
//    confirmed completion-flag caller (all unambiguously completion-
//    specific per that same text).
//  - type 2: fires the completion-flag writer again (same gating), then
//    would call the still-undecoded `0x006d9470` - explicitly OPEN per
//    Sec21.27b's own text ("a further, not-decoded function"), counted
//    but not invented.
//  - types 0/3 (shared handler): on success, would resolve the
//    mission's own `+0x1f8` second named Lua entry point and launch it
//    - this project has no real +0x1f8 name table (same gap as the
//    already-documented +0x1f0/script_mission_start real-name lookup,
//    Sec36.4), so only the ATTEMPT is counted, never invented.
// Every branch falls through to the shared phase-6 ("ending") tail.
bool EngineState::missionEndDispatch(const std::string& name, bool successFlag) {
        if (!missionContext_.missionBound || missionContext_.phase == 8 || name != missionContext_.boundMissionName)
            return false;
        ++missionContext_.missionEndDispatchCount;
        missionContext_.lastMissionEndDispatchSuccessFlag = successFlag;
        switch (missionContext_.boundMissionType) {
            case 1:
                if (successFlag) {
                    writeMissionCompletionFlagBits(name);
                    writeMissionUnlockedFlagBit(name);
                    ++missionContext_.missionEndType1UnlockAndCompleteCount;
                }
                break;
            case 2:
                if (successFlag) {
                    writeMissionCompletionFlagBits(name);
                }
                ++missionContext_.missionEndType2CompleteAndOpenStubCount; // 0x006d9470 - OPEN, counted only
                break;
            case 0:
            case 3:
            default:
                if (successFlag) ++missionContext_.missionEndType0Or3ScriptHookAttemptCount;
                break;
        }
        setMissionPhase(6);
        return true;
    }

bool EngineState::missionCompletionFlagBitSet(const std::string& name) const {
        auto it = missionFlagBits_.find(name);
        return it != missionFlagBits_.end() && (it->second & kMissionFlagCompleted) != 0;
    }

// Design notes moved verbatim from EngineState::armMissionEndTransitionTimer's declaration in engine_state.h
// (refactor step 3a):
// mission_end_failure's own dispatch (0x006da400) arming effect (Sec1.5,
// cross-cited by Sec3.5) - the only in-scope writer of timer 1.
// UPGRADED (Sec21.27c, 2026-10-09): also models the real 2000ms/4000ms
// doubling - 4000ms when this call arms timer 1 again while it is
// already armed from an earlier, not-yet-cleared dispatch; 2000ms
// otherwise. Called only from dispatchMissionEndPresentation()'s own
// non-coerced failure branch now (see below), not directly by the
// mission_end_failure stub.
void EngineState::armMissionEndTransitionTimer() {
        missionContext_.missionEndTimer1LastArmedDurationMs =
            missionContext_.missionEndTimer1Armed ? 4000 : 2000;
        missionContext_.missionEndTimer1Armed = true;
        ++missionContext_.missionEndFailureDispatchCount;
    }

// Design notes moved verbatim from EngineState::dispatchMissionEndPresentation's declaration in engine_state.h
// (refactor step 3a):
// `0x006da400` opened in full (Sec21.27c, 2026-10-09) - the ONE shared
// presentation dispatch both mission_end_failure's non-coerced path and
// mission_end_success reach after their own identical shared gate
// (0x006d7240: MissionContext::activeAndRunning/currentMissionName/
// currentMissionEnding) passes. `isSuccess` selects which of the two
// CONFIRMED branches runs: false = failure (stores `presentationText`
// into the fail-text stand-in, arms timer 1 for 2000/4000ms, and - only
// when hudTeardownGateBit reads true - fires the HUD-screen-teardown-all
// primitive); true = success (stores `presentationText` into the
// SEPARATE success-text stand-in, arms the SEPARATE timer 2, and -
// CONFIRMED asymmetry - NEVER fires the HUD-teardown primitive,
// regardless of the gate bit).
//
// Callers are expected to have already run the shared gate themselves
// (both existing stub functions already do, unchanged) - this method
// does not re-check it. mission_end_failure's own type-2 branch must
// skip this method ENTIRELY (routing to missionEndDispatch()/
// 0x006d9f70 instead, per Sec21.27c's own coercion finding, modeled in
// that stub, not here) rather than entering it and taking some branch
// inside - so this method has no coercion logic of its own.
void EngineState::dispatchMissionEndPresentation(bool isSuccess, const std::string& presentationText) {
        auto& mc = missionContext_;
        if (!isSuccess) {
            mc.lastFailReasonText = presentationText; // 0x014c7a08
            armMissionEndTransitionTimer();            // 0x12f2630, 2000/4000ms
            bool tearDown = mc.hudTeardownGateBit.known() && mc.hudTeardownGateBit.get();
            mc.lastDispatchTornDownHud = tearDown;
            if (tearDown) ++mc.hudTeardownCount; // 0x007b4990(-1) on registry 0x012fced8
        } else {
            mc.lastSuccessAnnouncementText = presentationText; // 0x014c846c
            armMissionEndSuccessTimer();                        // 0x12f2634
            // CONFIRMED asymmetry (Sec21.27c): success never tears down HUD.
            mc.lastDispatchTornDownHud = false;
            ++mc.missionEndSuccessDispatchCount;
        }
    }

// Design notes moved verbatim from EngineState::beginMissionPhaseTracking's declaration in engine_state.h
// (refactor step 3a):
// FIX 2026-10-08 (mission-blockers investigation, root cause of
// probe_blockers_ranked_20261008_post_resolver_merge.md item 2 -
// "mission context active, phase != 8 (0x014c8460/0x014c7a14) OPEN",
// 12/49 missions). spec-lua-api-behaviour.md Sec21.27a (2026-10-08, new)
// fully characterizes the real `0x014c8460`/`0x014c7a14` phase machine:
// phase 0 ("idle - no mission bound") is the real engine's own start-up
// value (`0x006d6010`, CONFIRMED); phase 8 ("the FIRST phase of a newly-
// bound mission") is set ONLY by the bind function `0x006d6920` and
// auto-resolves to phase 1 within a few frames - well before any script
// code, including a mission's own `_start`, is ever reachable. Every
// `!= 8` gate this project tracks (EngineState::MissionContext::
// activeAndRunning, InProgressType::activeMissionPresent,
// MissionExcludedRewardsState::activeAndRunning - three SEPARATELY
// SCOPED facades over the identical real pair of globals, see each
// field's own doc comment for why they are not merged into one) exists
// "specifically to keep scripts from mutating a mission that is still
// mid-bind," per that section's own text - NOT a steady/terminal value a
// host could ever legitimately observe once a mission's own script is
// actually running.
//
// Previously all three facades were left permanently OPEN (never set by
// any production path), so EVERY mission whose own `_start` script
// called a native gated on one of them - before this host had ever told
// EngineState a mission had begun - threw OpenStateError immediately,
// from inside `_start` itself (confirmed: all 12 blocked missions' own
// `first_error_message` is prefixed `<stem>_start: ...`). This method is
// the one hook this host's own mission driver (tools/lua_host_run.cpp's
// per-mission loop, immediately before its one `callMissionStart()` call)
// has for "a mission's own `_start` is about to be invoked" - which, per
// Sec21.27a's own table, is CONFIRMED to be well past the one-shot
// phase-8 window, so flipping all three gates to the real "ready" value
// here is not an invented default, it is the one honestly-reachable
// real phase value a host that has gotten this far could ever observe.
// The companion idle default (false, applied once at construction - see
// EngineState::EngineState()) is the OTHER real, CONFIRMED value
// (`0x006d6010`'s own phase-0/null-context start-up state) - together
// the two replace "permanently unknown" with the real engine's own two
// reachable states, inventing neither.
//
// UPGRADED this task (2026-10-08, Sec21.27a/Sec21.27b full-document
// read): this method no longer just flips the three legacy boolean
// facades - it also drives the REAL integer phase machine
// (bindMissionPhaseMachine(), MissionContext's own section above)
// through its real 8 -> (2, fresh+named) -> 1 -> (3 or 5) sequence, and
// (if `name` is flagged missionSilentClass()) the organic silent-class
// auto-success trigger. The three legacy facades are kept, set
// exactly as before, for every native that still gates on them
// directly (several do, e.g. set_mission_enemy_team,
// spec-lua-api-behaviour.md Sec21.11) - this is purely additive.
// `type` defaults to the CHOSEN mission-type 0 (see
// bindMissionPhaseMachine()'s own doc comment); `isRestart` defaults to
// false, matching this project's own kMissionStartIsRestart convention
// (tools/lua_host_run.cpp - every mission this host drives is a fresh
// start, never a restart).
//
// MISSION-HOST PHASE 3 (2026-10-09): this method (and bindMissionPhase-
// Machine() below it) are deliberately KEPT, not removed, even though
// tools/sr3_viewer.cpp's own production `--mission=<stem>` playtest bind
// now goes through the REAL sr3luahost::MissionPhaseMachine class
// (include/sr3luahost/menu_runtime.h) instead of calling this method
// directly - MissionPhaseMachine::launchStartFunction() itself calls
// THIS method, at the same real bind call site, once a mission's own
// `_start` is actually about to run (see that method's own doc comment,
// menu_runtime.h), so every legacy facade below still gets set through
// the real path. This method also has its own SEPARATE, still-legitimate
// direct caller: tools/lua_host_run.cpp's own standalone headless
// mission-drive loop (`host.engineState().beginMissionPhaseTracking(...)`,
// immediately before that file's own callMissionStart()) - a different
// tool with no MissionPhaseMachine instance of its own, not a dead
// parallel path left over from before this switch.
void EngineState::beginMissionPhaseTracking(const std::string& name, int32_t type,
                                    bool isRestart) {
        missionContext_.activeAndRunning.set(true);
        inProgressType_.activeMissionPresent.set(true);
        missionExcludedRewards_.activeAndRunning.set(true);
        // mission_set_excluded_reward gate (4) (spec-lua-api-behaviour.md Sec14.8, blocker m09, FINAL): the active
        // mission object's descriptor bit 0x8 at +0xc is class 51 `scripted_mission_start`, true for every story
        // mission ("started from a scripted_mission_start object"); activities and silent missions are ignored.
        // Every caller of this method launches a mission-package row from that object (see the comment above), so
        // the bit is true here.
        missionExcludedRewards_.descriptorBit8At0xc.set(true);
        bindMissionPhaseMachine(name, type, isRestart);
    }

int32_t EngineState::lookupAnimationStateId(const std::string& name) const {
        if (name.empty()) return -1;
        auto it = animationStateIds_.find(name);
        return it != animationStateIds_.end() ? it->second : -1;
    }

int EngineState::hudDisplayCreateState() {
        for (int i = 0; i < HudDisplayStatePool::kSlotCount; ++i) {
            if (!hudDisplayStatePool_.used[i]) {
                hudDisplayStatePool_.used[i] = true;
                hudDisplayStatePool_.removed[i] = false;
                ++hudDisplayStatePool_.createCount;
                // CONFIRMED (Sec15.24): only a SUCCESSFUL allocation opens
                // the record-and-replicate block.
                EngineState::replicateStateChange("hud_display_create_state", "");
                return HudDisplayStatePool::kFirstSlotId + i;
            }
        }
        return -1; // CONFIRMED exhaustion sentinel (Sec15.24)
    }

void EngineState::hudDisplayRemoveState(int slotId, bool stashBool) {
        // CONFIRMED (Sec15.12): the record-and-replicate call is
        // unconditional, fired before the local validation below.
        EngineState::replicateStateChange("hud_display_remove_state", "");
        ++hudDisplayStatePool_.removeCount;
        int idx = slotId - HudDisplayStatePool::kFirstSlotId;
        if (idx < 0 || idx >= HudDisplayStatePool::kSlotCount || !hudDisplayStatePool_.used[idx] ||
            hudDisplayStatePool_.removed[idx]) {
            ++hudDisplayStatePool_.removeInvalidCount;
            return;
        }
        hudDisplayStatePool_.removed[idx] = true;
        auto& q = hudDisplayStatePool_.pendingQueue;
        for (auto it = q.begin(); it != q.end(); ++it) {
            if (*it == slotId) { q.erase(it); break; }
        }
        hudDisplayStatePool_.lastStashedBool = stashBool;
    }

void EngineState::hudDisplayCommitState(int slotId, bool stashBool) {
        // CONFIRMED (Sec15.25): the record-and-replicate call is
        // unconditional, fired before the local enqueue below.
        EngineState::replicateStateChange("hud_display_commit_state", "");
        ++hudDisplayStatePool_.commitCount;
        int idx = slotId - HudDisplayStatePool::kFirstSlotId;
        bool validSlot = idx >= 0 && idx < HudDisplayStatePool::kSlotCount && hudDisplayStatePool_.used[idx] &&
                         !hudDisplayStatePool_.removed[idx];
        auto& q = hudDisplayStatePool_.pendingQueue;
        bool alreadyQueued = false;
        for (int v : q) if (v == slotId) { alreadyQueued = true; break; }
        if (!validSlot || alreadyQueued || q.size() >= 5) {
            ++hudDisplayStatePool_.commitRejectedCount;
            return;
        }
        q.push_back(slotId);
        hudDisplayStatePool_.lastStashedBool = stashBool;
    }

void EngineState::writeMissionCompletionFlagBits(const std::string& name) {
        uint8_t& bits = missionFlagBits_[name]; // starts 0 on first reference - CHOSEN, no CONFIRMED +0x88 construction value either way (same "write-only in this project's own observable scope" discipline as pedDensity_ above)
        bits = static_cast<uint8_t>((bits & ~kMissionFlagClearedByWriter) |
                                     (kMissionFlagCompleted | kMissionFlagCompletedSecondary));
        missionCompletedMap_[name] = true; // legacy facade - Sec15.10's own existing test coverage
        missionComplete_.set(name, true);  // legacy facade - Sec27.2/Sec20.14's own existing test coverage
    }

void EngineState::writeMissionUnlockedFlagBit(const std::string& name) {
        missionFlagBits_[name] |= kMissionFlagUnlocked;
        missionUnlocked_[name] = true; // legacy facade - Sec15.9's own existing test coverage
    }

bool EngineState::missionUnlockedValue(const std::string& name) const {
        auto it = missionUnlocked_.find(name);
        return it != missionUnlocked_.end() && it->second;
    }

// Design notes moved verbatim from EngineState::recordMissionSetCompleted's declaration in engine_state.h
// (refactor step 3a):
// CONFIRMED (Sec15.10): the "mission list changed" notify and the
// record-and-replicate call both fire regardless of resolve/
// idempotency. The REAL writer (0x006d6870) is itself unconditional on
// every call (Sec21.27b: "no gating condition before it") - the
// idempotency Sec15.10's own text describes belongs to mission_set_
// completed's OWN wrapper routine (0x006d6cf0's `+0xa0` check), not to
// 0x006d6870 itself; this project does not model that separate +0xa0
// field, so writing unconditionally here (same observable result as
// the old "skip if already true" guard, since both write the same
// value) matches the real unconditional writer more closely.
void EngineState::recordMissionSetCompleted(const std::string& name, bool resolved) {
        EngineState::replicateStateChange("mission_set_completed", resolved ? name : "");
        ++missionSetCompletedCallCount_;
        if (resolved) writeMissionCompletionFlagBits(name);
    }

bool EngineState::missionCompletedValue(const std::string& name) const {
        auto it = missionCompletedMap_.find(name);
        return it != missionCompletedMap_.end() && it->second;
    }

void EngineState::setPlayerSkydiveFallSpeed(double value) {
        if (coopLocalIsHost()) EngineState::replicateStateChange("skydive_set_player_dive_fall_speed", "");
        playerSkydiveFallSpeed_ = value;
    }

// Design notes moved verbatim from EngineState::setGameChoiceState's declaration in engine_state.h
// (refactor step 3a):
// game_choice_set_state (Sec15.26): persistent global bitmask
// 0x026e7e60, bit `index` set/cleared unconditionally. HOST-SAFETY:
// `index` is read as a byte (0-255, Sec15.26's own "kept as a byte")
// but the real bitmask is a 32-bit word - this project clamps the
// shift to avoid undefined behaviour in ITS OWN C++ for index >= 32
// (the spec text never states what the real engine does past bit 31
// either; no observable consequence either way since nothing in this
// project's scope reads a bit beyond 31 back). The two hardcoded
// special cases (index 0's notify; index 1's triple-gated achievement
// hook, 0x013123d1/0x029897a5/0x029897a6, all three OPEN/not modeled
// in this project) are counted but not claimed to have fired the real
// achievement event, since the 3 enable-flags this project would need
// to know are OPEN.
void EngineState::setGameChoiceState(uint8_t index, bool state) {
        if (coopLocalIsHost()) EngineState::replicateStateChange("game_choice_set_state", "");
        if (index < 32) {
            if (state) gameChoiceBitmask_ |= (1u << index);
            else gameChoiceBitmask_ &= ~(1u << index);
        } else {
            ++gameChoiceIndexOutOfRangeGuardCount_; // HOST-SAFETY, test-observable only
        }
        ++gameChoiceSetCount_;
        if (index == 0) ++gameChoiceIndex0NotifyCount_;
        if (index == 1) ++gameChoiceIndex1AchievementHookAttemptCount_;
    }

void EngineState::setDlcAchievementCounter(int64_t index, int32_t value) {
        if (index < 0) { ++dlcAchievementCounterNegativeIndexGuardCount_; return; } // CHOSEN (hardened divergence), Sec15.27
        if (index >= kDlcAchievementCounterSlotCount) return; // CONFIRMED real no-op (the `< 3` check), not a divergence
        dlcAchievementCounters_[static_cast<size_t>(index)] = value;
    }

int32_t EngineState::dlcAchievementCounterValue(int64_t index) const {
        if (index < 0 || index >= kDlcAchievementCounterSlotCount) return 0;
        return dlcAchievementCounters_[static_cast<size_t>(index)];
    }

std::string EngineState::languageString() const {
        if (currentLanguageIndex_ < 0) return ""; // CONFIRMED fallback (Sec16.1)
        static const char* kTable[12] = {"US", "ES", "IT", "JP", "DE", "FR", "NL", "SE", "DK", "CZ", "PL", "SK"};
        if (currentLanguageIndex_ < 12) return kTable[currentLanguageIndex_];
        throw OpenStateError("game_get_language table index >= 12 (floor, not confirmed full list)",
                             "spec-lua-api-behaviour.md Sec16.1");
    }

void EngineState::recordRespectGrant(bool targeted, bool notifyRequested) {
        EngineState::replicateStateChange("game_award_respect", ""); // CONFIRMED unconditional (opcode 0xa1 or targeted), not host-gated
        ++respectGrantCount_;
        if (targeted) ++respectGrantTargetedCount_; else ++respectGrantBroadcastCount_;
        // CORRECTED 2026-10-09 (spec-tables-progression.md Sec5.4, cross-
        // referenced into spec-lua-api-behaviour.md Sec16.2/Sec16.16's own
        // Changelog): this terminal call, `0x006ced90`, is NOT an
        // undecompiled "optional UI notification" as the old comment here
        // (and the two sections above it) characterized it - full
        // disassembly shows it is a SECOND invocation of the award routine
        // (0x00b97340/0x00b98d50) against up to two other save-shaped
        // progress blocks reached via globals 0x014c8574/0x014d8d1c, gated
        // by a co-op/session-state check - i.e. a co-op progression-mirror
        // ("mirror this respect award onto a split-screen partner's own
        // snapshot"), never a toast/popup of any kind. `0x014c8574` is
        // independently already named in spec-save-format.md Sec10.4 as a
        // retained snapshot-shaped buffer for a second player's state,
        // consistent with this reading. This project has no live
        // second-player save-shaped snapshot object to actually mirror
        // onto (SaveGameState, save_game_state.h, models only the LOCAL
        // player), so the mirror's own body is not implemented - only the
        // fact that a resolved call reached it is counted, honestly, same
        // "count only, real gate/body not modeled" convention as
        // tutorialUnlockCount/recordTutorialUnlock above.
        if (notifyRequested) ++respectGrantNotifyCount_;
    }

uint32_t EngineState::respectLevelCost(uint32_t level) const {
        if (level < respectLevelTable_.size()) {
            return static_cast<uint32_t>(respectLevelTable_[level].respect.value);
        }
        return 22000;
    }

// Design notes moved verbatim from EngineState::respectUnlockApplied's declaration in engine_state.h
// (refactor step 3a):
// Per-name live unlock flag (CONFIRMED: the global unlockables table
// `0x01522750`, record offset `+0xe1`) - CHOSEN per-name map, same "no
// generic hash table modelled" convention as triggerHoldToRepeat_
// above, since this project never populates the real unlockables.xtbl
// id space this flag is really keyed by (Sec4). Missing = not yet
// unlocked, matching the real all-zero-at-start bit state.
bool EngineState::respectUnlockApplied(const std::string& name) const {
        auto it = respectUnlockablesFlag_.find(name);
        return it != respectUnlockablesFlag_.end() && it->second;
    }

// Design notes moved verbatim from EngineState::statRecordOpenCount's declaration in engine_state.h
// (refactor step 3a):
// stat-system record-open count, per stat id (CONFIRMED: every respect
// award also opens a record for stat id 0x44, stats.xtbl, Sec1.1/
// Sec5.4) - this project has no pre-existing stats.xtbl/stat-id live
// tracking anywhere else to reuse (checked: no hit for "stat id" / achievement
// counters elsewhere in this header set beyond the unrelated DLC
// achievement-counter slots, Sec15.27), so this is this subsystem's own
// new per-id counter map, same "count only, real body not modeled"
// convention as tutorialAdvanceCounts_/tutorialUnlockCounts_ above -
// generalized per-id (not hardcoded to 0x44 alone) in case a later task
// finds another native driving the same pipeline.
uint64_t EngineState::statRecordOpenCount(int32_t statId) const {
        auto it = statRecordOpenCounts_.find(statId);
        return it != statRecordOpenCounts_.end() ? it->second : 0;
    }

// Design notes moved verbatim from EngineState::respectLifetimeCap's declaration in engine_state.h
// (refactor step 3a):
// The real per-award cap, CONFIRMED (Sec5.4, 2026-10-09 WAVE 10
// follow-up): a lifetime-total ceiling populated once at load by the
// respect_levels.xtbl loader as Sigma(row.Respect for all loaded rows)
// minus the LAST row's own Respect (238,875 with the real 50-row
// shipped table, L50's own Respect being 0). Computed from the loaded
// table directly rather than hardcoded, so a test-supplied table still
// produces the correct cap for its own data.
uint32_t EngineState::respectLifetimeCap() const {
        if (respectLevelTable_.empty()) return 0;
        uint32_t sum = 0;
        for (const auto& row : respectLevelTable_) sum += static_cast<uint32_t>(row.respect.value);
        return sum - static_cast<uint32_t>(respectLevelTable_.back().respect.value);
    }

void EngineState::commitRealRespectLevelUps(const std::vector<sr3progression::LevelUp>& ups) {
    for (const auto& up : ups) {
        for (const std::string& name : up.newlyApplied) {
            respectUnlockablesFlag_[name] = true;
            if (name == "HomieSlot1") ++respectHomieSlot1UnlockCount_;
        }
        if (up.milestone) runRespectDecadeMilestone(static_cast<uint32_t>(up.level));
        if (up.levelUpHint) postRespectLevelUpHint();
    }
}

void EngineState::awardRespect(int32_t amount) {
        if (amount <= 0) { ++respectAwardNonPositiveGuardCount_; return; }
        if (realRespectTableActive_) {
            // RP-6: spec-tables-progression.md B.3 item 1, the real routine over the real respect_levels.xtbl rows.
            // CHOSEN: the cash overflow (award at the cap / last level) is not routed to a wallet here (no wallet hook
            // on EngineState); AwardResult::overflowCashRaw is dropped. Question for Team A: which wallet field takes it?
            sr3progression::PlayerRespect p{static_cast<int32_t>(respectTotal_), static_cast<int32_t>(respectBanked_),
                                            static_cast<int32_t>(respectLevel_)};
            sr3progression::AppliedUnlocks applied;
            for (const auto& kv : respectUnlockablesFlag_) if (kv.second) applied.insert(kv.first);
            sr3progression::AwardContext ctx;
            ctx.inCityZone = respectDecadeMilestone_.inOpenWorldZone;
            const sr3progression::AwardResult res = sr3progression::AwardRespect(p, amount, realRespectTable_, applied, ctx);
            respectTotal_ = static_cast<uint32_t>(p.total);
            respectBanked_ = static_cast<uint32_t>(p.inLevel);
            respectLevel_ = static_cast<uint32_t>(p.level);
            if (res.granted > 0) recordStatRecordOpen(kRespectAwardStatId);
            commitRealRespectLevelUps(res.levelUps);
            return;
        }
        // CONFIRMED (Sec5.4): strictly after the amount>0 guard. If total >=
        // cap or level >= levelCount-1, the award grants nothing at all - a
        // full skip (no running-total update, no stat record, no level-up),
        // not a 0-clamp.
        const uint32_t cap = respectLifetimeCap();
        const size_t levelCount = respectLevelTable_.size();
        if (respectTotal_ >= cap || (levelCount > 0 && respectLevel_ >= levelCount - 1)) return;
        // Otherwise the actual grant is min(amount, cap - total), and it is
        // this CLAMPED grant - not the raw amount argument - that feeds both
        // the running total and the level-up routine.
        const uint32_t grant = std::min(static_cast<uint32_t>(amount), cap - respectTotal_);
        respectTotal_ += grant;
        recordStatRecordOpen(kRespectAwardStatId);
        applyRespectLevelUp(grant);
    }

// Design notes moved verbatim from EngineState::applyRespectLevelUp's declaration in engine_state.h
// (refactor step 3a):
// Level-up mechanism, CONFIRMED (Sec5.4): 0x009db8e0(playerObj, amount)
// loops: compares `+0x1ed4 + amount` against 0x0060eb60(playerObj) (the
// cost of the next level) - 0x0060eb60 is NOT a separate algorithm from
// Sec5.2's own 0x0060eb40/0x0060ebb0 pair, it is the exact same
// accessor wrapped to pull the level index from the player-object
// pointer first, so this reuses respectLevelCost() above directly,
// never a second cost table; while over threshold: increments
// `+0x1ed8`, calls the unlock-consumer and the decade-milestone hook
// below, and carries the remainder; caps at kRespectLevelCap.
void EngineState::applyRespectLevelUp(uint32_t amount) {
        if (realRespectTableActive_) {  // RP-6: library level-up loop (B.3), unlocks record L = the NEW level
            sr3progression::PlayerRespect p{static_cast<int32_t>(respectTotal_), static_cast<int32_t>(respectBanked_),
                                            static_cast<int32_t>(respectLevel_)};
            sr3progression::AppliedUnlocks applied;
            for (const auto& kv : respectUnlockablesFlag_) if (kv.second) applied.insert(kv.first);
            sr3progression::AwardContext ctx;
            ctx.inCityZone = respectDecadeMilestone_.inOpenWorldZone;
            const auto ups = sr3progression::ApplyLevelUp(p, static_cast<int32_t>(amount), realRespectTable_, applied, ctx);
            respectBanked_ = static_cast<uint32_t>(p.inLevel);
            respectLevel_ = static_cast<uint32_t>(p.level);
            commitRealRespectLevelUps(ups);
            return;
        }
        uint32_t remaining = respectBanked_ + amount;
        while (respectLevel_ < kRespectLevelCap) {
            const uint32_t cost = respectLevelCost(respectLevel_);
            if (remaining < cost) break;
            remaining -= cost;
            // CHOSEN indexing: Sec5.2's own CONFIRMED finding is specifically
            // about 0x0060eb40/0x0060ebb0's own "record L (0-based) is the
            // cost of the next level" mapping, not independently
            // re-confirmed for 0x0060ebf0's own call-site argument timing
            // (before vs. after this increment). This project reads the
            // record whose COST was just paid (0-based index == the level
            // count BEFORE this increment) as the same row that carries the
            // level just reached's own Unlockables, since Sec5.2 already
            // ties cost and unlockables to the same <respect_level> row -
            // the natural, non-fabricated reading, not independently
            // re-derived from 0x0060ebf0's own disassembly.
            const uint32_t reachedLevelRow = respectLevel_;
            ++respectLevel_;
            consumeRespectLevelUnlocks(reachedLevelRow);
            runRespectDecadeMilestone(respectLevel_);
            postRespectLevelUpHint();
        }
        respectBanked_ = remaining;
    }

// Design notes moved verbatim from EngineState::consumeRespectLevelUnlocks's declaration in engine_state.h
// (refactor step 3a):
// 0x0060ebf0(level) (Sec5.2/Sec5.4): for each of that row's Unlockable
// ids NOT YET APPLIED (CONFIRMED gate), calls the real shared "unlock
// this named thing" primitive 0x0071ccf0 (also used elsewhere for
// stronghold-completion unlocks, not reproduced here - only its own
// observable live-flag effect is modeled, same convention as every
// other "shared primitive, effect modeled not the primitive itself"
// field in this header) and flips the live per-name flag above -
// special-casing the literal name "HomieSlot1" with its own distinct
// notification (CONFIRMED).
void EngineState::consumeRespectLevelUnlocks(uint32_t row) {
        if (row >= respectLevelTable_.size()) return;
        for (const std::string& name : respectLevelTable_[row].unlockables) {
            if (respectUnlockApplied(name)) continue;
            respectUnlockablesFlag_[name] = true;
            if (name == "HomieSlot1") ++respectHomieSlot1UnlockCount_;
        }
    }

void EngineState::runRespectDecadeMilestone(uint32_t newLevel) {
        if (newLevel == 0 || newLevel % 10 != 0) return;
        if (!respectDecadeMilestone_.inOpenWorldZone) return;
        respectDecadeMilestone_.lastEventId = (newLevel == 50) ? 0x23 : 0x22;
        respectDecadeMilestone_.lastPosition = respectMilestonePlayerPosition_;
        ++respectDecadeMilestone_.fireCount;
    }

void EngineState::completionLoadLut(const std::string& name) {
        // _strnicmp-style 4-char CASE-INSENSITIVE PREFIX match (Sec16.4's
        // own text: "any string starting with those 4 characters... not
        // only an exact match").
        bool isNonePrefixed = name.size() >= 4 &&
            std::tolower(static_cast<unsigned char>(name[0])) == 'n' &&
            std::tolower(static_cast<unsigned char>(name[1])) == 'o' &&
            std::tolower(static_cast<unsigned char>(name[2])) == 'n' &&
            std::tolower(static_cast<unsigned char>(name[3])) == 'e';
        if (isNonePrefixed) {
            colorGradingLutSlots_[2] = {-1, 0.0};
        } else {
            colorGradingLutSlots_[2] = {static_cast<int32_t>(nextLutId_++), 1.0};
        }
    }

std::vector<EngineState::CustomizationOutfitWearRequest>& EngineState::customizationOutfitWearRequests() {
        return customizationOutfitWearRequests_;
    }

// Design notes moved verbatim from EngineState::dlcAchievementCounterGet's declaration in engine_state.h
// (refactor step 3a):
// waypoint_add (Sec12.24, 0x00a68cf0): CONFIRMED fixed -1.0 failure
// sentinel (the SAME kActionPlaySentinelResolveFail effect_play/the
// effect_play_on_* family use), overwritten with a plain 0.0 on "at
// least one success path" - the 3-tier-plus-fallback resolver priority
// chain collapses to this project's one shared objectResolves() gate
// (Sec29 convention), and the real numeric "waypoint id" question
// (Sec12.24's own OPEN item) is not guessed at - this project only ever
// produces the two CONFIRMED sentinel values. No new EngineState-level
// state needed beyond kActionPlaySentinelResolveFail (already shared
// with effect_play).

// get_current_hit_points (Sec12.25, 0x00a4bb00): the generic/follower/
// character-fallback-to-mount paths all CONFIRMED read the SAME +0x1cb8
// field CharacterState::currentHitPoints already models - reused
// directly, no new state needed there. The vehicle path's own distinct
// source is VehicleState::hitPointsForQuery above; the unidentified
// 4th-kind placed-object path is OPEN and not modeled (a stated gap,
// consistent with this project's existing placed-object boundary).

// =========================================================================
// Wave 2, Batch W2-3 (2026-10-08), spec-lua-api-behaviour.md Sec20+Sec21 -
// EngineState-level (not per-character/vehicle/placed-object) state. See
// lua_spec_confirmed_stubs_b16_wave2_sec20_21.cpp for full per-native
// citations.
// =========================================================================

// dlc_achievement_counter_get (Sec20.25): reads the SAME 3-slot array
// dlc_achievement_counter_set (Sec15.27) already writes
// (dlcAchievementCounters_). CONFIRMED: "1 number if index < 3; else
// pushes NOTHING (0 return values)" - this includes a NEGATIVE index
// (also "< 3"), which the REAL getter reads UNCHECKED - a genuine
// memory-safety OOB READ, distinct from the setter's own already-
// hardened OOB WRITE (setDlcAchievementCounter above). HARDENING POLICY
// (CHOSEN hardened divergence): index in [0,3) reproduces the real read
// exactly; index < 0 is refused (no safe way to know what address the
// real unchecked read would have touched) rather than fabricated, and
// counted via a NAMED per-site counter, separate from the setter's own
// (a different real bug, a different real call site); index >= 3 is the
// real, CONFIRMED "push nothing" case, not a divergence.
bool EngineState::dlcAchievementCounterGet(int64_t index, double& outValue) {
        if (index < 0) { ++dlcAchievementCounterGetNegativeIndexGuardCount_; return false; } // CHOSEN (hardened divergence), Sec20.25
        if (index >= kDlcAchievementCounterSlotCount) return false; // CONFIRMED real "push nothing" case
        outValue = static_cast<double>(dlcAchievementCounterValue(index));
        return true;
    }

// Design notes moved verbatim from EngineState::tutorialUnlockCount's declaration in engine_state.h
// (refactor step 3a):
// tutorial_unlock (Sec24.11): host-gated (coopLocalIsHost(), CONFIRMED
// false in single player); 0x007161a0's own internal write is OPEN (not
// independently decompiled) - this project does NOT invent which state
// code it writes into EngineState::tutorialState() above, only that a
// resolved call reached the setter is counted, per index, honestly (same
// convention as tutorialAdvanceCount/recordTutorialAdvance above).
int EngineState::tutorialUnlockCount(int index) const {
        auto it = tutorialUnlockCounts_.find(index);
        return it != tutorialUnlockCounts_.end() ? it->second : 0;
    }

std::optional<bool>& EngineState::missionStoreRecordPurchased(bool isProperty) {
        return isProperty ? missionPropertyPurchased_ : missionStorePurchased_;
    }

int32_t EngineState::radioStationForVehicle(const std::string& name) const {
        auto it = radioStationByVehicle_.find(name);
        return it != radioStationByVehicle_.end() ? it->second : -1;
    }

// Design notes moved verbatim from EngineState::applyNewGameDefaultOutfit's declaration in engine_state.h
// (refactor step 3a):
// New-game default outfit (spec-frontend-menu-runtime.md Sec9 item 1,
// CLOSED 2026-10-11): CONFIRMED the real script path is
// store_character.lua's store_character_lineup_to_customization() -
// the character-creation/preset-selection screen's own transition
// handler - which applies the player's chosen creation preset via
// pcr_apply_preset(preset_name, "figure", regional_preset_name, false)
// and immediately calls pcr_add_default_clothing() with the script's
// own comment: "We want the character to have clothes on at the top
// menu, and we need to change to preset clothing." This is NOT a
// separate xtbl-level "default outfit" field - no such field exists
// anywhere in this project's unlockables/player_presets models either
// (confirmed clean negative), so there is no competing path to remove
// here. Team B's CHOSEN application point for "new game, no save
// present" (same status as kNewGameStartingCashCents/
// grantNewGameAutoUnlocks above): mirrors that exact script call pair
// against the SAME pcr_apply_preset/pcr_add_default_clothing state the
// stubs already use (lua_spec_confirmed_stubs_b24_wave2_pcu_pcr.cpp),
// so a fresh game's outfit is always driven by whichever character-
// creation preset the player picked, never an xtbl default.
// Lower-folds presetName the SAME plain-ASCII way
// stub_pcr_apply_preset's own toLowerAsciiB24 does (AsciiLowerFold,
// zone_object_resolver.h, already included above), so this resolves
// against the identical presetResolves() keys tests already populate.
// The script calls pcr_add_default_clothing() unconditionally right
// after pcr_apply_preset() - it never checks a return value (there
// isn't one) - so the wipe/re-add step below is NOT gated on whether
// the preset itself resolved; only the returned bool (for test/caller
// visibility) reflects that.
bool EngineState::applyNewGameDefaultOutfit(const std::string& presetName) {
        const std::string key = AsciiLowerFold(presetName);
        const bool presetResolved = pcrPreset_.presetResolves.known(key) && pcrPreset_.presetResolves.get(key);
        if (presetResolved) ++pcrPreset_.appliedCount;
        ++pcuClothing_.addDefaultClothingSlot10RefreshCount; // CONFIRMED real effect ("refreshes worn slot 10")
        pcuInventory_.ownedItemCount = 0; // CONFIRMED-destructive wipe - FAITHFUL (Sec43.3), same target stub_pcr_add_default_clothing uses
        ++pcrAddDefaultClothingWipeCount_;
        return presetResolved;
    }

} // namespace sr3luahost
