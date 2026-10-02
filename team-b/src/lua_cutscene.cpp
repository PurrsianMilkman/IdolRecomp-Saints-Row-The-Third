// zscene lifecycle and the cutscene machine (spec-lua-api-behaviour.md
// Sec26.25, with Sec14.23 / Sec8.21; 2026-10-01 text through job nnlt).
// Declarations: engine_state.h ("zscene" and "cutscene machine" blocks).
//
// Labels: REAL = stated by Sec26.25 and CONFIRMED by its review status;
// CHOSEN = this host's own choice, stated where made; OPEN = not in the spec,
// refused through OpenStateError (open_state.h) - never a default.
//
// What is engine behaviour here (REAL):
//  - the prep gate 0x007232e0 and the teardown 0x00721c20(a, b, c);
//  - the reset-check 0x00720320 and the promotion 0x00720410, run by
//    0x007258a0 in cutscene states 0 and 2;
//  - the completion 0x007285c0 (load state 1: resident + soundtrack finished
//    -> 2, failed -> teardown; load state 0: the idle driver 0x00728440);
//  - the cutscene states 0..0xf as listed in item 6, and the two status
//    bytes 0x0153b525 / 0x0153b526.
// What is not:
//  - the frame itself: one call of cutsceneHostFrame per host tick, after the
//    fade frame (CHOSEN order; the engine's drivers 0x00702a50 / 0x00bdbc54
//    are not ordered against the fade routine in the spec), on the host clock
//    screenFadeClockMs();
//  - running 0x007285c0 every frame after the promotion: the Host summary's
//    instruction ("A host runs, every frame: promotion ..., then
//    completion"); the engine calls it from two unnamed cases of 0x0072d660;
//  - streaming, audio and world objects: the handle class, the soundtrack's
//    end and the nearest world object are OPEN values a test sets.
#include "sr3luahost/engine_state.h"

namespace sr3luahost {

namespace {

const char* const kSpec = "spec-lua-api-behaviour.md Sec26.25";

[[noreturn]] void refuse(const std::string& what) {
    throw OpenStateError(what, kSpec);
}

// REAL: the soundtrack completes "until it ends (status 0x66) or 5 s pass".
constexpr int64_t kSoundtrackTimeoutMs = 5000;
// REAL: "a 120 s stamp at 0x012f5d30" (state 5's resource loads).
constexpr int64_t kResourceLoadStampMs = 120000;

} // namespace

// ---------------------------------------------------------------------------
// Scene table and name resolution
// ---------------------------------------------------------------------------

void EngineState::installZsceneTable(std::vector<ZsceneTableRow> rows) {
    zsceneTable_ = std::move(rows);
    zsceneTableInstalled_ = true;
}

EngineState::ZsceneResolved EngineState::zsceneResolve(const std::string& name) const {
    ZsceneResolved r;
    if (zsceneTableInstalled_) {
        // 0x00721be0: CRC-32 of the lower-cased name (0x00d9e8b0, seed 0 -
        // sr3save::nameHash is that routine), linear scan, first match (REAL).
        const uint32_t crc = sr3save::nameHash(name);
        for (const ZsceneTableRow& row : zsceneTable_) {
            if (row.crc != crc) continue;
            r.found = true;
            r.key = zsceneTableKey(row.name);
            if (!row.kind) {
                refuse("kind (+0x8) of scene entry '" + row.name +
                       "' (CutsceneType absent or a case-only spelling variant)");
            }
            r.loadable = *row.kind == 1;
            return r;
        }
        r.key = zsceneTableKey(name); // missing from the installed table (REAL: no entry)
        return r;
    }
    // No table installed: only the per-name "kind-1 entry or not" map, OPEN
    // until a test sets it ("missing" and "kind != 1" answer alike everywhere).
    r.key = zsceneTableKey(name);
    r.loadable = zsceneLoadable_.get(r.key);
    r.found = r.loadable;
    return r;
}

// ---------------------------------------------------------------------------
// Teardown 0x00721c20(a, b, c), REAL (Sec26.25 lifecycle 2)
// ---------------------------------------------------------------------------

bool EngineState::zsceneCutsceneGuard() const {
    // Nothing happens if the cutscene manager *0x0153b528 exists with +8 == 1
    // and the cutscene state 0x0153b520 is 7..13 (REAL test; its purpose
    // "never unload under a playing cutscene" is HYPOTHESIS). Evaluated as an
    // AND whose answer is fixed by any known false part, so only the parts
    // that decide it are read.
    const OpenValue<int32_t>& cs = screenFade_.cutsceneState;
    if (cs.known() && (cs.get() < 7 || cs.get() > 13)) return false;
    if (cutsceneManager_.present.known() && !cutsceneManager_.present.get()) return false;
    if (cutsceneManager_.present.known() && cutsceneManager_.field8.known() && cutsceneManager_.field8.get() != 1)
        return false;
    // No known part is false: read the parts in order; the first OPEN one
    // refuses (a known state here is already in 7..13).
    if (!cs.known()) (void)cs.get();
    if (!cutsceneManager_.present.get()) return false;
    return cutsceneManager_.field8.get() == 1;
}

EngineState::TeardownKind EngineState::zsceneTeardownPlan(int c) const {
    // "It also does nothing if there is no current scene": a known empty
    // current decides it without the guard.
    if (zsceneCurrent_.known() && zsceneCurrent_.get().empty()) return TeardownKind::Nothing;
    if (zsceneCutsceneGuard()) return TeardownKind::Nothing;
    const std::string current = zsceneCurrent_.get();
    if (current.empty()) return TeardownKind::Nothing;
    // Handle class 1 (not live): acts only when c != 0.
    if (zsceneHandleClass_.get(current) == 1) return c != 0 ? TeardownKind::NotLive : TeardownKind::Nothing;
    return TeardownKind::Live;
}

void EngineState::zsceneTeardownApply(TeardownKind kind, int a, int b) {
    if (kind == TeardownKind::NotLive) {
        // Release 0x0153b71c / 0x0153b720, clear the current pointer and the
        // state, set 0x0153b541.
        zsceneSoundtrackActive_.set(false);
        zsceneCurrent_.set("");
        zsceneStateCode_.set(0);
        zsceneAutoSelectNearest_.set(true);
    } else if (kind == TeardownKind::Live) {
        const std::string current = zsceneCurrent_.get();
        // Release the secondary object 0x0153b534 (service 9), the lightset
        // when +0xac is set, and the selected handle (0x00dafad0): no
        // resources in this host. What 0x00dafad0 does to the handle's flag
        // word is OPEN (Sec26.25 review status: "the released handle's
        // class"), so the class becomes OPEN.
        zsceneHandleClass_.forget(current);
        if (a != 0) {
            // 0x007317a0(1): a writer of the soundtrack globals whose body is
            // not in the spec - the stream's state is OPEN afterwards.
            zsceneSoundtrackActive_.forget();
            zsceneSoundtrackStartMs_.forget();
            zsceneSoundtrackEnded_.forget();
        }
        zsceneAutoSelectNearest_.set(false);
        zsceneRequeueOnReset_.set(b != 0);
        zsceneStateCode_.set(0);
        // The current pointer is left as it was (REAL).
    }
}

// ---------------------------------------------------------------------------
// Prep gate 0x007232e0 and zscene_prep / zscene_is_loaded
// ---------------------------------------------------------------------------

EngineState::GatePlan EngineState::zsceneGatePlan(const std::string& name) const {
    GatePlan p;
    // Returns 0 when the entry is missing, its kind is not 1, or the
    // skip_all_cutscenes byte is set; 1 with no change when it is already
    // current (REAL, Sec26.25 lifecycle 1). The lookup comes first.
    const ZsceneResolved r = zsceneResolve(name);
    p.key = r.key;
    if (!r.loadable) return p;
    if (zsceneSkipAllCutscenes_.get()) return p;
    if (zsceneCurrent_.get() == r.key) return p;
    p.proceeds = true;
    p.teardown = zsceneTeardownPlan(0);
    return p;
}

void EngineState::zsceneGateApply(const GatePlan& plan) {
    if (!plan.proceeds) return;
    // 0x0101b530: a stub that does nothing. Then the teardown (1, 0, 0) of the
    // current scene, then the pending slot. The two per-object parameters
    // 0x0153b568 / 0x0153b56c (zero constants from Lua and cutscene starts,
    // object +0x8/+0xc from the auto-prep) are copied into the entry's
    // +0x18/+0x1c at promotion and read by nothing in the spec: not modelled.
    zsceneTeardownApply(plan.teardown, 1, 0);
    zscenePending_.set(plan.key);
}

void EngineState::zscenePrep(const std::string& name) {
    zsceneGateApply(zsceneGatePlan(name));
}

bool EngineState::zsceneIsLoaded(bool hasName, const std::string& name) {
    // Sec14.23 corrected truth table (REAL).
    if (hasName) {
        const ZsceneResolved r = zsceneResolve(name);
        if (!r.loadable) return true; // fast path 0x00723d20 false: nothing to load
        if (zsceneSkipAllCutscenes_.get()) return true;
        if (zsceneCurrent_.get() != r.key) {
            // The engine answers false; for the pending entry that lasts until
            // 0x007258a0 promotes it. If the host's last cutscene frame stopped
            // on an OPEN read, that promotion cannot run here: refuse naming it.
            if (cutsceneCounters_.lastFrameBlocked && zscenePending_.known() && zscenePending_.get() == r.key) {
                ++zscenePendingBlockedRefusals_;
                throw OpenStateError("promotion of pending zscene '" + r.key +
                                         "' (the per-frame driver 0x007258a0 last stopped on OPEN " +
                                         cutsceneCounters_.lastFrameBlocker + ")",
                                     kSpec);
            }
            return false;
        }
        return zsceneStateCode_.get() == 2;
    }
    if (zsceneSkipAllCutscenes_.get()) return true;
    return zsceneStateCode_.get() == 2;
}

// ---------------------------------------------------------------------------
// Reset-check 0x00720320, promotion 0x00720410, completion 0x007285c0
// ---------------------------------------------------------------------------

EngineState::ResetPlan EngineState::zsceneResetPlan() const {
    // True when there is no current entry (then 0x0153b541 := 1); when the
    // current entry's selected handle has class 1 (then state := 0, current :=
    // 0, and the entry is re-queued into the pending slot when 0x0153b542 is
    // set); or when the handle is live and 0x0153b541 is set (REAL).
    ResetPlan p;
    p.current = zsceneCurrent_.get();
    if (p.current.empty()) {
        p.result = true;
        p.write = ResetPlan::Write::SetAutoSelect;
        return p;
    }
    if (zsceneHandleClass_.get(p.current) == 1) {
        p.result = true;
        p.write = ResetPlan::Write::ResetNotLive;
        p.requeue = zsceneRequeueOnReset_.get();
        return p;
    }
    p.result = zsceneAutoSelectNearest_.get();
    return p;
}

void EngineState::zsceneResetApply(const ResetPlan& p) {
    if (p.write == ResetPlan::Write::SetAutoSelect) {
        zsceneAutoSelectNearest_.set(true);
    } else if (p.write == ResetPlan::Write::ResetNotLive) {
        zsceneStateCode_.set(0);
        zsceneCurrent_.set("");
        if (p.requeue) zscenePending_.set(p.current);
    }
}

bool EngineState::zsceneSoundtrackFinished() const {
    // 0x007316a0: the stream has ended (status 0x66) or 5 s have passed since
    // promotion started it (REAL). No stream: nothing to wait for. Exactly
    // 5 s: "pass" does not say which side of the comparison - OPEN.
    if (!zsceneSoundtrackActive_.get()) return true;
    const int64_t elapsed = screenFadeClockMs_ - zsceneSoundtrackStartMs_.get();
    if (elapsed == kSoundtrackTimeoutMs) refuse("soundtrack timer 0x0153b724 compared with 5 s at exact equality");
    if (elapsed > kSoundtrackTimeoutMs) return true;
    return zsceneSoundtrackEnded_.get();
}

void EngineState::zscenePromotionStep() {
    // 0x007258a0 in cutscene states 0 and 2 (REAL, Sec26.25 lifecycle 3,
    // CONFIRMED body): "it calls the reset-check 0x00720320 and, when that
    // returns true, promotes". The reset-check is called every time, so its
    // writes (0x0153b541 := 1 with no current entry; state := 0, current := 0
    // and the re-queue for a not-live current handle) happen whether or not
    // an entry is pending - a scene whose handle goes not-live while it loads
    // is reset (and re-queued with 0x0153b542) here, with nothing pending.
    // The promotion itself is limited to "a pending entry" (REAL, the Host
    // summary); what 0x00720410 does with an empty pending slot is not
    // described, so with nothing pending after the reset-check's own re-queue
    // only the reset-check runs. (Before 2026-10-01's continuation the host
    // skipped the reset-check too when nothing was pending.)
    // Every read happens before any write: an OPEN read refuses the frame
    // with no partial update.
    const ResetPlan reset = zsceneResetPlan();
    std::string promoted;
    if (reset.result) {
        // The re-queue copies the current entry into the pending slot,
        // replacing whatever was there (REAL: "0x00720320 copies the current
        // entry into it").
        promoted = reset.requeue ? reset.current : zscenePending_.get();
    }
    // 0x00720410 first waits until any previous soundtrack stream has
    // finished (0x007316a0); "waits" = no promotion this frame, the caller
    // tries again next frame (CHOSEN reading of "waits" for a per-frame step).
    const bool previousFinished = promoted.empty() ? true : zsceneSoundtrackFinished();
    zsceneResetApply(reset);
    if (promoted.empty() || !previousFinished) return;
    // Select the pending entry's live handle (+0xc, else +0x10) and start its
    // load (0x00dafea0) - the class map stands in for both; start the +0xf4
    // soundtrack (0x007315a0) and its timer; copy 0x0153b568/0x0153b56c into
    // +0x18/+0x1c; current := entry; pending := 0; state := 1.
    zsceneSoundtrackActive_.set(true);
    zsceneSoundtrackStartMs_.set(screenFadeClockMs_);
    zsceneSoundtrackEnded_.forget(); // a new stream's status: no audio here, OPEN
    zsceneCurrent_.set(promoted);
    zscenePending_.set("");
    zsceneStateCode_.set(1);
    ++cutsceneCounters_.zscenePromotions;
}

void EngineState::zsceneCompletionStep() {
    const int state = zsceneStateCode_.get();
    if (state == 1) {
        // Classify the current entry's selected handle (0x00dafb60): class 3
        // (resident) with a finished or 5-second-old soundtrack -> state 2 and
        // 0x0153b534 (an object from slot +0x5c of the service-9 object, not
        // modelled); class 5 (failed) -> teardown 0x00721c20(1, 0, 0); any
        // other class waits (REAL).
        const std::string current = zsceneCurrent_.get();
        if (current.empty()) refuse("0x007285c0 with load state 1 and no current entry (not described)");
        const int cls = zsceneHandleClass_.get(current);
        if (cls == 3) {
            if (zsceneSoundtrackFinished()) {
                zsceneStateCode_.set(2);
                ++cutsceneCounters_.zsceneCompletions;
            }
        } else if (cls == 5) {
            const TeardownKind k = zsceneTeardownPlan(0);
            zsceneTeardownApply(k, 1, 0);
            ++cutsceneCounters_.zsceneFailedLoads;
        }
        return;
    }
    if (state == 0) {
        // Idle driver 0x00728440 (REAL): with 0x0153b541 set it finds the
        // nearest scene-bearing world object (list 0x03171a64) and preps that
        // object's entry through 0x00737780 -> 0x007232e0(entry, obj +0x8,
        // obj +0xc); otherwise it runs the reset 0x00720320.
        if (zsceneAutoSelectNearest_.get()) {
            const std::string nearest = zsceneNearestWorldObjectScene_.get();
            if (nearest.empty()) return;
            const GatePlan p = zsceneGatePlan(nearest);
            zsceneGateApply(p);
            if (p.proceeds) ++cutsceneCounters_.zsceneIdleAutoPreps;
            return;
        }
        zsceneResetApply(zsceneResetPlan());
        return;
    }
    // Load state 2: Sec26.25 describes 0x007285c0's work for states 1 and 0
    // only; nothing is done here.
}

// ---------------------------------------------------------------------------
// The cutscene machine: 0x0072d660 then 0x007258a0 (Sec26.25 item 6)
// ---------------------------------------------------------------------------

void EngineState::cutsceneSetState(int32_t s) {
    screenFade_.cutsceneState.set(s);
    ++cutsceneCounters_.stateTransitions;
}

void EngineState::cutsceneMachineStep() {
    const int32_t cs = screenFade_.cutsceneState.get();
    switch (cs) {
    case 0:  // idle
    case 2:  // the load wait is 0x007258a0's case
    case 5:  // the resource loads are 0x007258a0's case
    case 15: // chain / teardown is 0x007258a0's case
        return;
    case 1:
        // "1 -> 3 (CS_STATE_FADING_OUT)" - no condition is stated. (Who
        // writes 1 is OPEN, so only a test reaches this case.)
        cutsceneSetState(3);
        return;
    case 3:
        // "3 -> 4 once the player-side checks pass"; the checks are not
        // described: an OPEN predicate.
        if (cutscenePlayerChecksPass_.get()) cutsceneSetState(4);
        return;
    case 4:
        // "4: 0x007258a0 mounts preload_items.vpp / preload_effects.vpp and
        // sets 0x0153b543, then 0x0072d660 -> 5". State 4 is entered by this
        // routine (3 -> 4) and 0x007258a0 runs after it in the same frame, so
        // the mount has happened by the time this case runs.
        cutsceneSetState(5);
        return;
    case 6:
        refuse("cutscene state 6: body of the loading helper 0x0072c790");
    case 7:
        refuse("cutscene state 7: body of the loading helper 0x0072bff0");
    case 8:
        refuse("cutscene state 8: bodies of 0x00720200 + 0x0072c260");
    case 9:
        refuse("cutscene state 9: body of the playback start 0x00729150");
    case 10:
    case 11:
    case 12:
        refuse("cutscene state " + std::to_string(cs) + ": body of the playback helper 0x0072c980");
    case 13: {
        // CS_STATE_STOP: the manager's +0x35cc becomes the chain target
        // 0x0153b52c (REAL) - done; what follows (the writer of state 14) is
        // OPEN, so the frame stops after the copy.
        if (!cutsceneManager_.present.get()) refuse("cutscene state 13 with no cutscene manager (not described)");
        cutsceneChainTarget_.set(cutsceneManager_.chainTarget.get());
        refuse("cutscene state 13 (CS_STATE_STOP): the transition after it (the writer of state 14)");
    }
    case 14:
        refuse("cutscene state 14: the test 0x00722e00 that moves it to 15");
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
        // Memory unlock / STOPPED / FINAL_STREAMING / fade-in and free: read
        // by elimination from the jump table, HIGH CONFIDENCE only.
        refuse("cutscene state " + std::to_string(cs) +
               " (0x10..0x13 read by elimination, HIGH CONFIDENCE only - not cleared for implementation)");
    default:
        refuse("cutscene state " + std::to_string(cs) + " (outside the 20-entry jump table at 0x0072defc)");
    }
}

void EngineState::cutsceneSecondStep() {
    const int32_t cs = screenFade_.cutsceneState.get();
    // The two status bytes, rewritten every frame (REAL): "a cutscene is
    // playing" (state 10..13) and "a cutscene is in progress" (state >= 2
    // with a manager).
    const bool playing = cs >= 10 && cs <= 13;
    const bool inProgress = cs >= 2 && cutsceneManager_.present.get();
    cutscenePlayingByte_.set(playing);
    cutsceneInProgressByte_.set(inProgress);

    switch (cs) {
    case 0:
        zscenePromotionStep();
        return;
    case 2:
        // "2 waits for the zscene to load, then 0x00725df0 continues": the
        // promotion runs here too; loaded = load state 0x0153b51c == 2 (the
        // value 0x0072d660 tests). What 0x00725df0 does next is not in the
        // spec beyond the prep gate and the fade-out request: OPEN.
        zscenePromotionStep();
        if (zsceneStateCode_.get() == 2) {
            refuse("cutscene state 2: what 0x00725df0 (cutscene_play) does after the zscene has loaded");
        }
        return;
    case 4:
        // Mount the two preload packfiles and set 0x0153b543 (REAL). No
        // packfile system here: logged.
        cutsceneMountLog_.push_back("preload_items.vpp");
        cutsceneMountLog_.push_back("preload_effects.vpp");
        cutscenePreloadMounted_.set(true);
        return;
    case 5: {
        // "starts the resource loads (0x00722f10 ...) -> 6, or for a loaded
        // zscene -> 7" (REAL). "Loaded zscene" read as: the manager's scene
        // is a kind-1 entry, it is the current entry and the load state is 2
        // (the zscene_is_loaded truth table without the skip byte).
        const std::string key = cutsceneManager_.sceneKey.get();
        const ZsceneResolved r = zsceneResolve(key);
        const bool loadedZscene = r.loadable && zsceneCurrent_.get() == r.key && zsceneStateCode_.get() == 2;
        if (loadedZscene) {
            cutsceneSetState(7);
            return;
        }
        // 0x00722f10: the shared npc_basehead resource, the manager's handle,
        // every +0x20 resource, the +0xf4 soundtrack; a 120 s stamp.
        std::vector<std::string> loads = {"npc_basehead", "cutscene manager handle"};
        const ZsceneTableRow* row = nullptr;
        for (const ZsceneTableRow& t : zsceneTable_) {
            if (zsceneTableKey(t.name) == r.key) {
                row = &t;
                break;
            }
        }
        if (row && row->resources) {
            for (const std::string& res : *row->resources) loads.push_back(res);
        } else {
            loads.push_back("(+0x20 resources: OPEN for this entry)");
        }
        loads.push_back("+0xf4 soundtrack");
        for (std::string& l : loads) cutsceneLoadLog_.push_back(std::move(l));
        zsceneSoundtrackActive_.set(true);
        zsceneSoundtrackStartMs_.set(screenFadeClockMs_);
        zsceneSoundtrackEnded_.forget();
        cutsceneLoadStamp_.set(screenFadeClockMs_ + kResourceLoadStampMs);
        // 0x00722f10 is also a writer of 0x0153b530, 0x0153b51c and
        // 0x0153b541 / 0x0153b542 (Globals table); the values it writes are
        // not given, so all four are OPEN from here.
        zsceneCurrent_.forget();
        zsceneStateCode_.forget();
        zsceneAutoSelectNearest_.forget();
        zsceneRequeueOnReset_.forget();
        cutsceneSetState(6);
        return;
    }
    case 15: {
        // "either chains into 0x0153b52c (manager rebuilt by 0x00725670 -> 5)
        // or begins teardown -> 0x10" (REAL). The rebuild's body is not in the
        // spec: the manager's fields are OPEN afterwards.
        const std::string chain = cutsceneChainTarget_.get();
        if (!chain.empty()) {
            cutsceneManager_.sceneKey.forget();
            cutsceneManager_.field8.forget();
            cutsceneManager_.chainTarget.forget();
            cutsceneSetState(5);
        } else {
            cutsceneSetState(0x10);
        }
        return;
    }
    default:
        return;
    }
}

void EngineState::cutsceneHostFrame() {
    ++cutsceneCounters_.framesRun;
    try {
        cutsceneMachineStep();
        cutsceneSecondStep();
        zsceneCompletionStep();
        cutsceneCounters_.lastFrameBlocked = false;
    } catch (const OpenStateError& e) {
        ++cutsceneCounters_.framesBlockedOnOpen;
        cutsceneCounters_.lastFrameBlocked = true;
        cutsceneCounters_.lastFrameBlocker = e.global();
    }
}

std::string EngineState::cutsceneFrameSummary() const {
    const CutsceneCounters& c = cutsceneCounters_;
    std::string s = "frames:" + std::to_string(c.framesRun) + " blocked_on_open:" + std::to_string(c.framesBlockedOnOpen) +
                    " state_transitions:" + std::to_string(c.stateTransitions) +
                    " zscene_promotions:" + std::to_string(c.zscenePromotions) +
                    " zscene_completions:" + std::to_string(c.zsceneCompletions) +
                    " zscene_failed_loads:" + std::to_string(c.zsceneFailedLoads) +
                    " zscene_idle_auto_preps:" + std::to_string(c.zsceneIdleAutoPreps) +
                    " pending_blocked_refusals:" + std::to_string(zscenePendingBlockedRefusals_);
    if (!c.lastFrameBlocker.empty()) s += " last_blocker=[" + c.lastFrameBlocker + "]";
    return s;
}

} // namespace sr3luahost
