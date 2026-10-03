// zscene lifecycle and the cutscene machine (spec-lua-api-behaviour.md
// Sec26.25, with Sec14.23 / Sec8.21; 2026-10-01 text through job nnlt, plus
// the 2026-10-02 `mm_p_01` zscene-promotion-driver correction below).
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
//  - streaming, audio and world objects: the handle class, the soundtrack's
//    end and the nearest world object are OPEN values a test sets.
//
// 2026-10-02 correction (the `mm_p_01` zscene-promotion-driver investigation,
// Sec26.25's corrected Host summary): completion and promotion are REAL,
// unconditional-every-frame steps, each independent of the other and of the
// rest of the 20-state cutscene machine - "a host runs, every gameplay frame
// and regardless of the cutscene machine, but only in cutscene states 0 and
// 2: FIRST completion ..., THEN promotion ...". This host previously read the
// cutscene state (0x0153b520) once, up front, in a single try block shared by
// all three steps (the 20-state machine, the 5-case second step holding
// promotion, and completion last) - an OPEN read there (cutscene state has no
// specced start-up value here, Sec26.25 Globals, and nothing in gameplay ever
// sets it for a mission that never starts a real cutscene) aborted the whole
// frame, silently skipping completion and promotion too, even though neither
// of those actually needs that global (0x007285c0 switches on the ZSCENE load
// state 0x0153b51c; 0x00720320/0x00720410 switch on the pending/current slots
// - cutscene state only decides *whether* 0x007258a0 is the one driving them
// this frame, same as the old code's own case 0/2 routing already had it).
// That is exactly the "drives these two steps only while a cutscene is
// already active" hazard the corrected Host summary names, in this host's
// own shape, and it is why a bare `zscene_prep` (no cutscene ever started,
// so cutscene state stays permanently OPEN here) could never be promoted.
// Fixed below: completion and promotion each get their own try/catch (one
// step's OPEN block no longer blocks the other), completion runs first, and
// promotion's "am I in cutscene state 0 or 2" gate treats an OPEN cutscene
// state as "proceed" rather than "refuse" - there is no writer anywhere in
// this host that could have moved it away from the engine's own idle default
// without the gate itself already knowing it (cutsceneSetState's only
// callers are this file's own state-machine transitions below, none reachable
// without already knowing the state), so an OPEN read here is not missing
// evidence of an active story cutscene, it is the absence of one. The 20-state
// machine and the second step's other 3 cases (4, 5, 15) still require the
// cutscene state to actually be known - they cannot do anything with it
// otherwise, and nothing above depends on them running.
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
    // 2026-10-02 correction: stamp when THIS name became pending, so
    // zsceneIsLoaded's refusal below only ever fires on a frame that has
    // actually run for it (see zscenePendingSetAtFrame_'s own comment).
    if (!plan.key.empty()) zscenePendingSetAtFrame_ = cutsceneCounters_.framesRun;
}

void EngineState::zscenePrep(const std::string& name) {
    zsceneGateApply(zsceneGatePlan(name));
}

// ---------------------------------------------------------------------------
// cutscene_play_do / cutscene_play_check_done (spec-lua-api-behaviour.md
// Sec57.2, "ranking tranche 23"; real-hit batch 2026-10-03 - these two are
// 2 of the 8 real-hit names found across the 10 orchestrator-directed
// sections Sec50-Sec59, by far the highest-volume pair: 31 + 12121 calls in
// a fresh mission drive, with `cutscene_play_check_done` the single highest
// hit count of the whole batch, and the real Lua stack evidence
// ("waiting_at=...cutscene_play") shows this pair is where almost every
// mission in this project's own mission-drive baseline parks.
//
// CONFIRMED body, per Sec26.25 item 6/Host summary (written before
// Sec57.2's own Lua name was known, under the label "0x00725df0,
// 'cutscene_play'"): "use the same prep gate [0x007232e0, i.e. the SAME
// mechanism zscene_prep already runs] with the zero constants and then
// request a 500 ms fade-out." Sec57.2 itself adds the fuller native's own
// argument shape and three logic defects on top of that body:
//  - a 2nd (optional table-or-boolean) argument is "read and discarded" -
//    modeled by simply never reading it for any decision (functionally
//    identical to reading-then-ignoring it; real call-site evidence from
//    this project's own mission drive passes `nil` here);
//  - a destination table (3rd argument) needs EXACTLY two entries or
//    "both end-of-cutscene teleports are silently dropped";
//  - "the destinations are written even when the cutscene start itself
//    was refused" (another cutscene already running) - modeled by writing
//    them unconditionally, BEFORE the refusal check below.
// NOT modeled (Sec57.2 itself marks both HYPOTHESIS/not given beyond the
// crash shape, and nothing in this project's own in-scope natives can ever
// observe either): the uninitialized 8-entry "script-group handles" array
// copied into the manager, and the failed-scene-manager-allocation
// null-write path. Fabricating either would be inventing an engine value
// this task's own standing rule refuses to do.
void EngineState::cutscenePlayDo(const std::string& name, std::vector<std::string> destinations) {
    // CONFIRMED (Sec57.2): written unconditionally, before anything below
    // decides whether the start itself succeeds or is refused.
    if (destinations.size() == 2) {
        cutscenePlayDestination1_ = destinations[0];
        cutscenePlayDestination2_ = destinations[1];
    } else {
        cutscenePlayDestination1_.clear();
        cutscenePlayDestination2_.clear();
    }
    // "the cutscene start itself was refused (e.g. another cutscene is
    // already running)" - this host has no native in its real-hit scope
    // that ever ends a cutscene, so cutscenePlayInProgress_ is a one-way
    // latch here (CHOSEN, not a spec fact beyond the refusal condition
    // itself): once a start has succeeded, a further call is refused,
    // same as the real engine's own "already running" gate.
    if (cutscenePlayInProgress_) return;
    // Same prep gate zscene_prep already runs, with the zero constants
    // (CONFIRMED, Sec26.25 - this call IS that same mechanism, not a
    // reimplementation of it).
    zsceneGateApply(zsceneGatePlan(name));
    // "request a 500 ms fade-out" (CONFIRMED) - the SAME screenFadeRequest()
    // mechanism fade_out's own stub uses, with no callback and no special
    // flag (flag 0, the established convention for a plain request - see
    // stub_fade_out/stub_fade_in in lua_spec_confirmed_stubs.cpp).
    screenFadeRequest(true, 500, nullptr, 0);
    cutscenePlayInProgress_ = true;
}

bool EngineState::zsceneIsLoaded(bool hasName, const std::string& name) {
    // Sec14.23 corrected truth table (REAL).
    if (hasName) {
        const ZsceneResolved r = zsceneResolve(name);
        if (!r.loadable) return true; // fast path 0x00723d20 false: nothing to load
        if (zsceneSkipAllCutscenes_.get()) return true;
        if (zsceneCurrent_.get() != r.key) {
            // The engine answers false; for the pending entry that lasts until
            // 0x007258a0 promotes it. If the entry is still pending after a
            // cutscene frame that ran *for this pending entry* (framesRun has
            // advanced since it was set - 2026-10-02 correction: EngineState
            // is shared across a whole mission-drive run, so a stale blocked
            // flag from an earlier, unrelated mission's own frames must not
            // count here) hit an OPEN read somewhere in that frame's own
            // completion/promotion/machine steps (cutsceneHostFrame), the
            // promotion that would have cleared the pending slot cannot be
            // assumed to have run: refuse naming it. Queried synchronously,
            // before any frame of its own has run yet, it is simply "not
            // promoted yet" - the same false the engine itself would answer.
            if (cutsceneCounters_.lastFrameBlocked && cutsceneCounters_.framesRun > zscenePendingSetAtFrame_ &&
                zscenePending_.known() && zscenePending_.get() == r.key) {
                ++zscenePendingBlockedRefusals_;
                throw OpenStateError("promotion of pending zscene '" + r.key +
                                         "' (the per-frame zscene driver last stopped on OPEN " +
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
        // CORRECTED 2026-10-02 (Sec26.25 Globals table, the `mm_p_01`
        // zscene-promotion investigation): 0x00720320 sets 0x0153b541 to 1
        // unconditionally on this class-1 reset path - the earlier reading
        // (jobs mnao/nnlt) misread this as copying 0x0153b542's value, which
        // this project had therefore left unwritten here (0x0153b542's own
        // separate requeue-into-pending effect, below, is unaffected).
        zsceneAutoSelectNearest_.set(true);
        if (p.requeue) {
            zscenePending_.set(p.current);
            // Same 2026-10-02 correction as zsceneGateApply: this is a fresh
            // entry into the pending slot too.
            if (!p.current.empty()) zscenePendingSetAtFrame_ = cutsceneCounters_.framesRun;
        }
    }
}

bool EngineState::zsceneSoundtrackFinished() const {
    // 0x007316a0: the stream has ended (status 0x66) or 5 s have passed since
    // promotion started it (REAL). No stream: nothing to wait for. Exactly
    // 5 s: "pass" does not say which side of the comparison - OPEN.
    // A stream that was never started (0x0153b71c/0x0153b720/0x0153b724 only
    // come into existence at a promotion, Sec26.25 Globals) is "nothing to
    // wait for" by construction of this host's own run, not an engine fact
    // it is missing - the very first promotion attempt (2026-10-02, `mm_p_01`
    // investigation) must not refuse here just because nothing has recorded
    // whether a stream is active yet. The slot itself stays OPEN either way.
    if (!zsceneSoundtrackActive_.known() || !zsceneSoundtrackActive_.get()) return true;
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
        // Promotion itself now runs unconditionally from cutsceneHostFrame,
        // before this function is even reached (2026-10-02 correction, file
        // header above) - nothing left to do for this case specifically.
        return;
    case 2:
        // Promotion already ran (same reasoning). "2 waits for the zscene to
        // load, then 0x00725df0 continues": loaded = load state 0x0153b51c
        // == 2 (the value 0x0072d660 tests). What 0x00725df0 does next is not
        // in the spec beyond the prep gate and the fade-out request: OPEN.
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
    // 2026-10-02 correction (file header above, Sec26.25's corrected Host
    // summary): completion and promotion are each unconditional, independent
    // per-frame steps - neither one's own OPEN block may stop the other from
    // being attempted, and completion runs first. Only the rest of the
    // cutscene machine (the full 20-state jump table, and the second step's
    // other 3 cases) still needs the cutscene state to actually be known.
    ++cutsceneCounters_.framesRun;
    bool blocked = false;
    std::string blocker;

    // 1. Completion (0x007285c0): never depends on the cutscene state.
    try {
        zsceneCompletionStep();
    } catch (const OpenStateError& e) {
        blocked = true;
        blocker = e.global();
    }

    // 2. Promotion (0x007258a0/0x00720320/0x00720410): acts only in cutscene
    // states 0 and 2 (REAL). An OPEN cutscene state is "proceed", not
    // "refuse" (file header above) - nothing in this host could have moved
    // it away from the engine's idle default without this gate already
    // knowing about it.
    const bool csKnown = screenFade_.cutsceneState.known();
    bool runPromotion = true;
    int32_t cs = 0;
    if (csKnown) {
        cs = screenFade_.cutsceneState.get();
        runPromotion = (cs == 0 || cs == 2);
    }
    if (runPromotion) {
        try {
            zscenePromotionStep();
        } catch (const OpenStateError& e) {
            blocked = true;
            blocker = e.global();
        }
    }

    // 3. The rest of the cutscene machine needs the real cutscene-state
    // value to do anything at all; skip entirely while it is OPEN, same as
    // before this correction - nothing above depends on it running. These
    // two keep their original, single try/catch (not split like steps 1/2
    // above): cutsceneSecondStep unconditionally reads the cutscene manager
    // for any cs >= 2 (its in-progress status byte), so if cutsceneMachineStep
    // already refused on that same cutscene state, cutsceneSecondStep's own
    // read would just as reliably refuse too, on a different, less specific
    // global - the original "stop at the first refusal" chaining between
    // these two is unrelated to this correction and is preserved here.
    if (csKnown) {
        try {
            cutsceneMachineStep();
            cutsceneSecondStep();
        } catch (const OpenStateError& e) {
            blocked = true;
            blocker = e.global();
        }
    }

    cutsceneCounters_.lastFrameBlocked = blocked;
    if (blocked) {
        ++cutsceneCounters_.framesBlockedOnOpen;
        cutsceneCounters_.lastFrameBlocker = blocker;
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
