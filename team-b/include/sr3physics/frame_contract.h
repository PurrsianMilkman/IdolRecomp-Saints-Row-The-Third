#pragma once

// sr3physics/frame_contract.h - the Havok boundary's per-frame contract: the
// Delta-t clamp, the interlocked physics window and its three mutation
// categories (queue-activation, queue-body-state, drop-setter), the step's
// job fan-out shape, and the frame driver that sequences all of it.
//
// Source: spec-physics-runtime.md Sec4.3 ("The per-frame driver"), Sec4.4
// ("The step and the job fan-out"), Sec4.6 ("The frame contract, summarized
// for a replacement") and Sec5 (the global-constants table), SPEC TEAM
// (cleanroom reverse-engineering process) - the ONLY source for every
// behaviour below. Addresses and counts in comments are copied from that
// text so a reader can cross-check the section; nothing here was derived
// from the executable, and no decompiled code or pseudocode appears below.
//
// WHAT IS IMPLEMENTED:
//   * Delta-t clamp to [1/120, 1/20] s, then divided by a time scale
//     (Sec4.3 "Headline correction" / Sec5 row 0x0132a0ac) - the SHIPPED
//     contract: one variable physics step per game frame. The step-mode
//     flag (Sec5 row 0x012f8c1e) that would instead select a fixed 1/30 s
//     accumulator is HIGH unreachable in the shipped build (its only writer
//     is never referenced anywhere in the binary, Sec4.3) - NOT modelled,
//     by the same "do not build the dead path" reading the spec itself
//     gives it.
//   * PhysicsWindow: the interlocked in-progress COUNTER (Sec4.3 step 2/6,
//     Sec5 row 0x0161d394, Sec7) - explicitly NOT a lock; isOpen() is a
//     plain nonzero test, matching "an interlocked in-progress counter, not
//     a critical section/lock" (Sec4.3).
//   * The three mutation categories while the window is open (Sec4.3 "The
//     physics window: deferred and dropped mutations", a hard contract):
//       1. ActivationQueue - appended to a plain queue, replayed once the
//          driver's own internal path runs (see runFrame()'s doc comment
//          for how the "still inside the window" (Sec4.3 step 5) vs.
//          "replayed after the window closes" (Sec4.3 hard-contract prose)
//          phrasings are reconciled here).
//       2. BodyStateQueue<T> - a SECOND queue, KEYED BY BODY-TABLE INDEX
//          (Sec4.3: "a second queue of body-state changes (keyed by body-
//          table index)"), replayed the same way but RE-CHECKING each
//          body's activation state before acting - a property the spec
//          states only for the replayed path, so the immediate (window-
//          closed) path here does NOT re-check (CHOSEN: literal reading).
//       3. DroppedSetter - "three further wrappers (position/velocity-style
//          setters) do nothing at all inside the window" (Sec4.3): modelled
//          as one generic drop-while-open mechanism, since the spec gives
//          the COUNT (3) and the shared behaviour, not three individual
//          identities beyond "position/velocity-style" (Sec4.3) - see the
//          hand-off report for why three separate named wrappers would be
//          inventing identities the spec does not give.
//   * IPhysicsBackend / IGameJobScheduler: the step's own call shape
//     (Sec4.4, CONFIRMED) - begin into Havok -> submit 2 game jobs (job
//     count static, Sec4.4 "Inputs") on the game's own scheduler, each
//     draining the SAME Havok job queue (Sec4.4 "the worker job
//     0x00754dd0") -> drain on the calling thread too -> finish into Havok
//     with a NULL thread pool, deliberately (Sec4.4 step 5) -> wait for the
//     2 jobs. NullPhysicsBackend / InlineGameJobScheduler are the trivial
//     single-threaded stand-ins this unit is scoped to build (Scope
//     discipline: no real Havok, no real multithreaded scheduling) - they
//     still reproduce the OBSERVABLE call shape (begin/drain/finish/wait,
//     exactly 2 jobs submitted and both complete before step() returns).
//   * PostWindowDeferredQueue: the further deferred-operation queue replayed
//     AFTER the window closes (Sec4.3 step 7 / Sec4.6 item 3), "through 8
//     more Havok calls" - modelled as a generic callable queue (see its own
//     doc comment for why its CONTENTS are not asserted).
//   * PhysicsFrameDriver: sequences one frame exactly as Sec4.3's "The
//     driver, in order" lists it.
//
// WHAT IS NOT IMPLEMENTED (OPEN / out of scope):
//   * The fixed 1/30 s accumulator path and the sub-step-cap/multiplier
//     globals that only feed it (Sec5 rows 0x0161d380, 0x012f8c18,
//     0x012f8c14) - dead code in the shipped build (Sec4.3), and Sec6 OPEN
//     item 3 (when vehicle code sets k = 2.0) has "no effect in the shipped
//     default mode" per the spec's own words.
//   * Water/buoyancy, ragdoll and vehicle pre-step internals, and the real
//     identity split between the two post-step hooks (Sec6 OPEN item 4:
//     "Post-step roles of 0x0077c9d0 and 0x007699a0 - likely contact-event
//     dispatch and handing the stepped time to gameplay" - HIGH, not
//     CONFIRMED which does which) - each exposed here as an optional hook
//     with its HIGH-confidence role in the comment, not forced together.
//   * Where the constraint filter's listener sub-object is added (Sec6 OPEN
//     item 1) and the construction-info field names (Sec6 OPEN item 2) -
//     bring-up internals with no effect on this file's observable contract;
//     not modelled at all (see lifecycle.h for the bring-up/shutdown ORDER,
//     which is the part of bring-up this unit's scope covers).
//   * Real collision/solver code, real Havok, real multithreaded job
//     scheduling (Scope discipline).

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sr3physics {

// --- Delta-t clamp (Sec4.3, Sec5) -------------------------------------------

// Boot-time clamps, from 0x005d1a30 (Sec4.3, CONFIRMED).
inline constexpr double kMinDt = 1.0 / 120.0;
inline constexpr double kMaxDt = 1.0 / 20.0;

// "rewritten every frame by the game's own frame timer 0x00db9970 ...,
// clamped to a configurable range and divided by a game time scale"
// (Sec4.3, quoted literally). CHOSEN reading: the "configurable range" and
// the "[1/120, 1/20] s" boot-time clamp two sentences later are the SAME
// clamp (the spec never names a second range), and the order is clamp-then-
// divide, taken from the sentence's own word order - not independently
// confirmed which happens first. timeScale = 1.0 (the default) makes this
// just the clamp, which is what every test below exercises; a timeScale of
// 0 is guarded (returns the clamped value unchanged) since the spec does
// not describe that case and dividing by zero is not a value to invent.
double ClampDt(double rawFrameDt, double timeScale = 1.0);

// --- the physics window (Sec4.3 steps 2/6, Sec5 row 0x0161d394, Sec7) ------

// "An interlocked in-progress counter, not a critical section/lock -
// confirmed by import resolution" (Sec4.3). enter()/leave() model
// InterlockedIncrement/InterlockedDecrement(0x0161d394); isOpen() is the
// plain nonzero test every mutation wrapper below makes.
class PhysicsWindow {
public:
    void enter() { counter_.fetch_add(1, std::memory_order_acq_rel); }
    void leave() { counter_.fetch_sub(1, std::memory_order_acq_rel); }
    bool isOpen() const { return counter_.load(std::memory_order_acquire) != 0; }
    int32_t value() const { return counter_.load(std::memory_order_acquire); }

private:
    std::atomic<int32_t> counter_{0};
};

// --- mutation category 1: activation requests (Sec4.3 hard contract #1) ---

using BodyHandle = uint32_t;  // the game's 0-4999 body-table slot index (Sec4.1 step 12)

// "Activation requests are appended to a queue instead of calling Havok
// immediately" (Sec4.3) - a PLAIN list, not keyed (unlike BodyStateQueue
// below): the spec's own wording distinguishes "appended to a queue" from
// the body-state queue's explicit "keyed by body-table index", so this one
// is not deduplicated.
class ActivationQueue {
public:
    using ApplyFn = std::function<void(BodyHandle, bool)>;

    void enqueue(BodyHandle body, bool activate) { pending_.push_back({body, activate}); }

    // The driver's own internal replay path (see PhysicsFrameDriver::runFrame).
    void replay(const ApplyFn& applyDirect) {
        for (auto& e : pending_) applyDirect(e.body, e.activate);
        pending_.clear();
    }

    size_t pendingCount() const { return pending_.size(); }

private:
    struct Entry {
        BodyHandle body;
        bool activate;
    };
    std::vector<Entry> pending_;
};

// --- mutation category 2: body-state changes (Sec4.3 hard contract #2) ----

// "A second queue of body-state changes (keyed by body-table index) is
// replayed the same way, re-checking each body's activation state before
// acting" (Sec4.3). T is the port's own change payload - this boundary unit
// does not know the real Havok body-state field layout (that is the
// water/ragdoll/vehicle units' own material); T is left generic so this
// library asserts only the KEYING and RE-CHECK discipline, never invented
// field values.
template <typename T>
class BodyStateQueue {
public:
    using ApplyFn = std::function<void(BodyHandle, const T&)>;
    using IsActiveFn = std::function<bool(BodyHandle)>;

    // Keyed by body-table index: a second request for the same body before
    // replay overwrites the first (CHOSEN - the natural reading of "keyed",
    // since the spec does not say whether a repeat stacks or replaces).
    void enqueue(BodyHandle body, T change) { pending_[body] = std::move(change); }

    // "Replayed the same way, re-checking each body's activation state
    // before acting": a body that is no longer active by replay time is
    // skipped, not applied.
    void replay(const ApplyFn& applyDirect, const IsActiveFn& isActive) {
        for (auto& kv : pending_) {
            if (isActive(kv.first)) applyDirect(kv.first, kv.second);
        }
        pending_.clear();
    }

    size_t pendingCount() const { return pending_.size(); }

private:
    std::unordered_map<BodyHandle, T> pending_;
};

// A deliberately opaque body-state payload for a port/test that has no real
// Havok body-state fields to carry (CHOSEN stand-in, not a recovered record
// layout - see this header's own top-of-file note).
struct BodyStateChange {
    int tag = 0;
    double value = 0.0;
};

// --- mutation category 3: dropped setters (Sec4.3 hard contract #3) -------

// "Three further wrappers (position/velocity-style setters) do nothing at
// all inside the window - any such request made during the step is simply
// lost, by design" (Sec4.3). One generic drop-while-open mechanism; a port
// instantiates it per concrete setter it needs (see this header's top-of-
// file note on why three separate named types are not modelled).
class DroppedSetter {
public:
    explicit DroppedSetter(const PhysicsWindow& window) : window_(&window) {}

    // Returns true if applied (window was closed), false if silently
    // dropped (window was open) - "by design", not an error.
    template <typename Fn>
    bool tryApply(Fn&& apply) const {
        if (window_->isOpen()) return false;
        std::forward<Fn>(apply)();
        return true;
    }

private:
    const PhysicsWindow* window_;
};

// --- the further, post-window deferred-operation queue (Sec4.3 step 7) ----

// "After the window: replays a further deferred queue through 8 more Havok
// calls" (Sec4.3 step 7 / Sec4.6 item 3). The spec gives the COUNT (8 Havok
// calls) and the ORDER (after the window, before periodic memory upkeep),
// not which wrapper populates it or what the 8 calls are (Sec6 OPEN item 4
// only names the two G-only post-step hooks, not this queue's own writer) -
// modelled as a generic callable queue so the frame driver reproduces the
// ORDER without inventing the content.
class PostWindowDeferredQueue {
public:
    using Op = std::function<void()>;

    void push(Op op) { pending_.push_back(std::move(op)); }

    void flush() {
        for (auto& op : pending_) op();
        pending_.clear();
    }

    size_t pendingCount() const { return pending_.size(); }

private:
    std::vector<Op> pending_;
};

// --- the step's job fan-out shape (Sec4.4, CONFIRMED) ----------------------

// "Inputs: Delta-t, a job count (static 2) ..." (Sec4.4).
inline constexpr int kStepJobCount = 2;

// The Havok side of the step (Sec4.4 steps 1, 4, 5): begin/drain/finish.
// drainJobs() stands in for BOTH the calling thread's own drain (step 4) and
// the worker job 0x00754dd0's drain of the same queue (step 4's own HIGH
// note) - a single-threaded port has nothing to distinguish them by.
class IPhysicsBackend {
public:
    virtual ~IPhysicsBackend() = default;
    virtual void beginStep(double dt) = 0;  // 0x00c17920, HIGH hkpWorld::initMtStep
    virtual void drainJobs() = 0;           // 0x00be88c0, HIGH hkJobQueue::processAllJobs
    virtual void finishStep() = 0;          // 0x00c16fc0 with a NULL thread pool, HIGH hkpWorld::finishMtStep
};

// The trivial stand-in this unit is scoped to build: no real Havok exists
// here, so every call is a documented no-op that still preserves the call
// SHAPE (a spy subclass in the test file verifies ORDER and COUNT).
class NullPhysicsBackend : public IPhysicsBackend {
public:
    void beginStep(double) override {}
    void drainJobs() override {}
    void finishStep() override {}
};

// The game's own scheduler (Sec4.4 steps 2-3, 6): "open a job group, then
// submit 2 jobs on the game's own scheduler (class-3 queue) ... wait for the
// 2 game jobs to complete". No real scheduler exists in this project (Scope
// discipline: no real multithreaded job scheduling).
class IGameJobScheduler {
public:
    virtual ~IGameJobScheduler() = default;
    virtual void submitJob(std::function<void()> job) = 0;
    virtual void waitAll() = 0;
};

// Single-threaded stand-in: runs every submitted job inline, so waitAll()
// always finds them already done. Preserves the OBSERVABLE contract (step()
// submits exactly kStepJobCount jobs and both complete before it returns)
// without a real 3-thread fan-out (Sec4.4: "at most 3 threads").
class InlineGameJobScheduler : public IGameJobScheduler {
public:
    void submitJob(std::function<void()> job) override {
        if (job) job();
        ++submitted_;
    }
    void waitAll() override {}
    uint32_t submittedCount() const { return submitted_; }

private:
    uint32_t submitted_ = 0;
};

// 0x0075c80 (Sec4.4, CONFIRMED): begin -> submit kStepJobCount jobs (each
// drains the job queue) -> drain on this thread too -> finish with a null
// thread pool -> wait for the jobs.
void RunPhysicsStep(IPhysicsBackend& backend, IGameJobScheduler& scheduler, double dt);

// --- the per-frame driver (Sec4.3 "The driver, in order" / Sec4.6 item 2) -

class PhysicsFrameDriver {
public:
    PhysicsFrameDriver(IPhysicsBackend& backend, IGameJobScheduler& scheduler);

    PhysicsWindow window;
    ActivationQueue activationQueue;
    BodyStateQueue<BodyStateChange> bodyStateQueue;
    PostWindowDeferredQueue postWindowQueue;

    // What "calling Havok immediately" (window closed) or the driver's own
    // internal replay (window open, deferred) means for a port. Left unset
    // (no-op) by default.
    std::function<void(BodyHandle, bool)> applyActivation;
    std::function<bool(BodyHandle)> queryIsActive;
    std::function<void(BodyHandle, const BodyStateChange&)> applyBodyStateChange;

    // Sec4.3 steps 3-5 hooks. Each optional (no-op if unset); internals out
    // of scope (water/ragdoll/vehicle pre-step units; Sec6 OPEN item 4 for
    // the post-step pair).
    std::function<void(double dt)> waterPreStep;      // Sec4.3 step 3
    std::function<void(double dt)> ragdollPreStep;    // Sec4.3 step 3
    std::function<void(double dt)> vehiclePreStep;    // Sec4.3 step 4, exactly once
    std::function<void(double steppedDt)> handOffToGameplay;  // Sec4.3 step 5 / Sec6 OPEN 4
    std::function<void()> dispatchContactEvents;               // Sec4.3 step 5 / Sec6 OPEN 4
    std::function<void()> periodicMemoryUpkeep;                 // Sec4.3 step 7 (~30 s watchdog-style; throttling is the caller's own timer, not modelled here)

    // Public wrapper #1 (Sec4.3 hard contract #1): queues while the window
    // is open, else calls applyActivation directly.
    void requestActivation(BodyHandle body, bool activate);
    // Public wrapper #2 (hard contract #2): queues (keyed by body index)
    // while open, else calls applyBodyStateChange directly (no re-check on
    // the immediate path - see BodyStateQueue's own doc comment).
    void requestBodyStateChange(BodyHandle body, BodyStateChange change);
    // Convenience factory for hard-contract category #3 (a port names its
    // own concrete setters; see DroppedSetter's own doc comment).
    DroppedSetter makeDroppedSetter() const { return DroppedSetter(window); }

    // One frame, in Sec4.3's own order:
    //   enter window -> water pre-step -> ragdoll pre-step -> vehicle
    //   pre-step (exactly once) -> the step itself (Sec4.4) -> hand-off to
    //   gameplay -> replay (activation, then body-state) -> contact
    //   dispatch -> leave window -> post-window queue flush -> periodic
    //   memory upkeep.
    //
    // Design note on the two queues' replay point: Sec4.3's own numbered
    // list places the replay of the activation and body-state queues at
    // step 5, "post-step, STILL INSIDE the window" (step 6, the decrement,
    // comes after). A separate paragraph in the same section ("The physics
    // window: deferred and dropped mutations") instead says these same two
    // queues are "replayed AFTER the window closes". Both are taken
    // literally here by giving the replay path its OWN direct-apply
    // callbacks (applyActivation / applyBodyStateChange) that never
    // consult window.isOpen() - only the public wrappers (requestActivation
    // / requestBodyStateChange) do that check. So: the replay runs before
    // the decrement (matching the numbered steps, literally), but because
    // it bypasses the gated public wrapper, it can never re-queue itself,
    // and by the time any GAME CODE caller can observe its effect the
    // window has, in every externally visible sense, already done its job
    // of deferring that mutation - which is what "replayed after the
    // window closes" means from a caller's vantage point. A design that
    // instead ran the replay through the public wrapper would livelock (an
    // open window re-queueing its own replay forever), which the spec's
    // behaviour obviously does not do - so this is read as the only
    // consistent resolution, not an invented value.
    double runFrame(double rawFrameDt, double timeScale = 1.0);

private:
    IPhysicsBackend& backend_;
    IGameJobScheduler& scheduler_;
};

}  // namespace sr3physics
