// Synthetic tests for the physics runtime boundary - the frame contract of
// spec-physics-runtime.md Sec4.3/Sec4.4/Sec4.5/Sec4.6 and the character-
// proxy lock-step pass cap / listener queue-only contract of Sec2.1-Sec2.2,
// cross-referenced to spec-player-runtime.md Sec5.6.4/Sec5.8. Every expected
// value below is a rule quoted from those sections (cited per block); every
// backend/scheduler/record type here is a synthetic stand-in, not data.
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "sr3physics/character_proxy.h"
#include "sr3physics/frame_contract.h"
#include "sr3physics/job_dispatch.h"
#include "sr3physics/lifecycle.h"

using namespace sr3physics;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

bool feq(double a, double b, double eps = 1e-9) { return std::fabs(a - b) < eps; }

// ===================================================================
// Delta-t clamp (spec-physics-runtime.md Sec4.3, Sec5 row 0x0132a0ac)
// ===================================================================
void testDtClamp() {
    // Boot-time clamps: [1/120, 1/20] s (Sec4.3).
    CHECK(feq(ClampDt(1.0 / 1000.0), kMinDt));  // far below min -> clamps up
    CHECK(feq(ClampDt(1.0), kMaxDt));            // far above max -> clamps down
    CHECK(feq(ClampDt(1.0 / 60.0), 1.0 / 60.0)); // a normal 60 Hz frame is inside the range, unchanged
    CHECK(feq(ClampDt(kMinDt), kMinDt));          // exactly at the lower bound
    CHECK(feq(ClampDt(kMaxDt), kMaxDt));          // exactly at the upper bound
    CHECK(feq(ClampDt(0.0), kMinDt));             // a hitch/zero frame still clamps up
    CHECK(feq(ClampDt(-5.0), kMinDt));            // a nonsensical negative frame still clamps up

    // "... and divided by a game time scale" (Sec4.3, literal) - applied
    // AFTER the clamp.
    CHECK(feq(ClampDt(1.0 / 60.0, 2.0), (1.0 / 60.0) / 2.0));
    CHECK(feq(ClampDt(1.0, 2.0), kMaxDt / 2.0));  // clamp first, then divide
    CHECK(feq(ClampDt(1.0 / 60.0, 0.0), 1.0 / 60.0));  // timeScale 0 guarded, not a divide-by-zero
}

// ===================================================================
// The physics window (Sec4.3 steps 2/6, Sec5 row 0x0161d394, Sec7): an
// interlocked COUNTER, not a lock.
// ===================================================================
void testPhysicsWindow() {
    PhysicsWindow w;
    CHECK(!w.isOpen() && w.value() == 0);
    w.enter();
    CHECK(w.isOpen() && w.value() == 1);
    w.enter();  // a counter tolerates re-entry; a lock would deadlock here
    CHECK(w.isOpen() && w.value() == 2);
    w.leave();
    CHECK(w.isOpen() && w.value() == 1);  // still open: the counter is nonzero
    w.leave();
    CHECK(!w.isOpen() && w.value() == 0);
}

// ===================================================================
// Mutation category 1: activation requests queue while open, replay once
// closed (Sec4.3 hard contract #1).
// ===================================================================
void testActivationQueueCategory() {
    PhysicsWindow w;
    ActivationQueue q;
    std::vector<std::pair<BodyHandle, bool>> applied;
    auto apply = [&](BodyHandle b, bool activate) { applied.emplace_back(b, activate); };

    // Window open: a request is appended, NOT applied.
    w.enter();
    if (w.isOpen()) {
        q.enqueue(7, true);
        q.enqueue(9, false);
    } else {
        apply(7, true);
    }
    CHECK(applied.empty());
    CHECK(q.pendingCount() == 2);

    // Window still open: the request is NOT a lock, so a second caller can
    // also queue (not deadlock, not apply).
    q.enqueue(11, true);
    CHECK(q.pendingCount() == 3);

    // Replay (the driver's own internal path): every queued request is now
    // observably applied, in order.
    q.replay(apply);
    CHECK(applied.size() == 3);
    CHECK((applied[0] == std::pair<BodyHandle, bool>{7, true}));
    CHECK((applied[1] == std::pair<BodyHandle, bool>{9, false}));
    CHECK((applied[2] == std::pair<BodyHandle, bool>{11, true}));
    CHECK(q.pendingCount() == 0);

    // Window closed: a request applies immediately, no queueing at all.
    w.leave();
    applied.clear();
    if (!w.isOpen()) apply(42, true);
    CHECK(applied.size() == 1 && q.pendingCount() == 0);
}

// ===================================================================
// Mutation category 2: body-state changes, keyed by body-table index,
// replayed with a re-check of each body's activation state (Sec4.3 hard
// contract #2).
// ===================================================================
void testBodyStateQueueCategory() {
    BodyStateQueue<BodyStateChange> q;
    std::vector<BodyHandle> applied;
    auto apply = [&](BodyHandle b, const BodyStateChange&) { applied.push_back(b); };

    // Keyed by body-table index: a second request for the SAME body before
    // replay overwrites the first, it does not stack into two entries.
    q.enqueue(3, BodyStateChange{1, 10.0});
    q.enqueue(3, BodyStateChange{2, 20.0});
    q.enqueue(5, BodyStateChange{3, 30.0});
    CHECK(q.pendingCount() == 2);  // one entry for body 3, one for body 5

    // Replay re-checks activation: body 3 went inactive since it was
    // queued, body 5 is still active - only body 5's change is applied.
    auto isActive = [](BodyHandle b) { return b != 3; };
    q.replay(apply, isActive);
    CHECK(applied.size() == 1 && applied[0] == 5);
    CHECK(q.pendingCount() == 0);
}

// ===================================================================
// Mutation category 3: the three position/velocity-style setters do
// nothing at all inside the window - a request made during the step is
// simply lost, by design (Sec4.3 hard contract #3).
// ===================================================================
void testDroppedSetterCategory() {
    PhysicsWindow w;
    DroppedSetter setPositionLike(w);
    int applyCount = 0;
    auto apply = [&] { ++applyCount; };

    // Window open: the setter call is OBSERVABLY LOST - not queued, not
    // replayed later, just gone.
    w.enter();
    CHECK(setPositionLike.tryApply(apply) == false);
    CHECK(applyCount == 0);
    w.leave();
    // Still gone after the window closes - unlike the first two
    // categories, there is no replay to recover it.
    CHECK(applyCount == 0);

    // Window closed: the same call now applies.
    CHECK(setPositionLike.tryApply(apply) == true);
    CHECK(applyCount == 1);

    // A second, independently-constructed setter sharing the same window
    // (modelling the spec's "three further wrappers" sharing one counter).
    DroppedSetter setVelocityLike(w);
    w.enter();
    CHECK(setVelocityLike.tryApply(apply) == false);
    w.leave();
    CHECK(applyCount == 1);  // still 1: the velocity-style call was dropped too
}

// ===================================================================
// The step's job fan-out shape (Sec4.4, CONFIRMED): begin -> submit 2 jobs
// (each drains) -> drain on this thread too -> finish with a null thread
// pool -> wait.
// ===================================================================
// Logs into a SHARED, externally-owned vector (rather than one of its own)
// so a caller that also logs its own hook calls (testFrameDriverFullFrame)
// can observe one single, truly interleaved order across both.
class SpyPhysicsBackend : public IPhysicsBackend {
public:
    explicit SpyPhysicsBackend(std::vector<std::string>& sharedLog) : log(sharedLog) {}
    std::vector<std::string>& log;
    double lastDt = -1.0;
    void beginStep(double dt) override {
        log.push_back("begin");
        lastDt = dt;
    }
    void drainJobs() override { log.push_back("drain"); }
    void finishStep() override { log.push_back("finish"); }
};

void testPhysicsStepJobShape() {
    std::vector<std::string> log;
    SpyPhysicsBackend backend(log);
    InlineGameJobScheduler scheduler;
    RunPhysicsStep(backend, scheduler, 1.0 / 30.0);

    // Exactly kStepJobCount (2) jobs submitted, both complete before
    // RunPhysicsStep returns (Sec4.4: "a job count (static 2)" ... "wait
    // for the 2 jobs to complete").
    CHECK(scheduler.submittedCount() == kStepJobCount);
    CHECK(kStepJobCount == 2);

    // Order: begin, then kStepJobCount drains (the 2 jobs) plus 1 more (the
    // calling thread's own drain, Sec4.4 step 4), then finish. 3 drains
    // total here since the stand-in scheduler runs jobs inline.
    std::vector<std::string> expected = {"begin", "drain", "drain", "drain", "finish"};
    CHECK(log == expected);
    CHECK(feq(backend.lastDt, 1.0 / 30.0));
}

// ===================================================================
// The per-frame driver (Sec4.3 "The driver, in order" / Sec4.6 item 2):
// full-frame integration of all three mutation categories plus ordering.
// ===================================================================
void testFrameDriverFullFrame() {
    std::vector<std::string> log;
    SpyPhysicsBackend backend(log);
    InlineGameJobScheduler scheduler;
    PhysicsFrameDriver driver(backend, scheduler);

    bool windowOpenDuringHandOff = false;
    bool windowOpenDuringActivationApply = false;
    bool windowOpenDuringPostWindowFlush = true;  // should become observably false

    driver.waterPreStep = [&](double) { log.push_back("water"); };
    driver.ragdollPreStep = [&](double) { log.push_back("ragdoll"); };
    driver.vehiclePreStep = [&](double) {
        log.push_back("vehicle");
        // A mutation requested while the window is open, from inside the
        // frame itself - exercises the live queue/drop categories in situ.
        driver.requestActivation(1, true);
        driver.requestBodyStateChange(2, BodyStateChange{9, 1.0});
    };
    driver.handOffToGameplay = [&](double) {
        log.push_back("handoff");
        windowOpenDuringHandOff = driver.window.isOpen();
    };
    driver.applyActivation = [&](BodyHandle b, bool) {
        log.push_back("applyActivation:" + std::to_string(b));
        windowOpenDuringActivationApply = driver.window.isOpen();
    };
    driver.queryIsActive = [](BodyHandle) { return true; };
    driver.applyBodyStateChange = [&](BodyHandle b, const BodyStateChange&) {
        log.push_back("applyBodyState:" + std::to_string(b));
    };
    driver.dispatchContactEvents = [&] { log.push_back("contacts"); };
    driver.postWindowQueue.push([&] {
        log.push_back("postWindowOp");
        windowOpenDuringPostWindowFlush = driver.window.isOpen();
    });
    driver.periodicMemoryUpkeep = [&] { log.push_back("upkeep"); };

    const double dt = driver.runFrame(1.0 / 30.0);
    CHECK(feq(dt, 1.0 / 30.0));

    // Sec4.3's own order: pre-steps, then the step, then hand-off, then
    // replay (activation before body-state, matching the numbered list's
    // own order), then contact dispatch, then (after the window) the
    // post-window queue, then periodic upkeep.
    std::vector<std::string> expected = {"water", "ragdoll", "vehicle", "begin", "drain", "drain",
                                          "drain", "finish",  "handoff", "applyActivation:1",
                                          "applyBodyState:2", "contacts", "postWindowOp", "upkeep"};
    CHECK(log == expected);

    // The window is still (numerically) open while the hand-off and the
    // replay run - "still inside the window" (Sec4.3 step 5) - and closed
    // by the time the post-window queue flushes (Sec4.3 step 7).
    CHECK(windowOpenDuringHandOff == true);
    CHECK(windowOpenDuringActivationApply == true);
    CHECK(windowOpenDuringPostWindowFlush == false);

    // The window itself is closed once the frame returns.
    CHECK(!driver.window.isOpen());

    // A mutation requested OUTSIDE any frame (window closed) applies at
    // once, with no queueing.
    log.clear();
    driver.requestActivation(99, false);
    CHECK(log == std::vector<std::string>{"applyActivation:99"});
}

// ===================================================================
// The Havok job queue's own per-job-type dispatch table (job_dispatch.h,
// spec-physics-runtime.md Sec9, unit W-U0): the row storage contract
// (registerJobType), the "not re-checked at dispatch time" routing, the
// "no soft-fail path" fault for an unregistered slot, the write-once
// seal() discipline, and the one genuinely OPEN value (the job-type count
// N, Sec9.2 OPEN item 36).
// ===================================================================
void testJobTypeRowStorageContract() {
    JobTypeTable table;

    // Sec9.2: a never-registered type's row is still the bootstrap
    // default - NULL table (zero length), both side-funcs still default.
    CHECK(!table.isRegistered(5));
    const JobTypeRow& defaultRow = table.rowFor(5);
    CHECK(defaultRow.subtypeTableLength() == 0);
    CHECK(defaultRow.procFuncIsDefault());
    CHECK(defaultRow.finishFuncIsDefault());
    CHECK(!defaultRow.handlerFor(0));  // NULL subtypeTable -> every slot empty

    // registerJobType's own storage contract (Sec9.1, CONFIRMED): the
    // table/length are stored exactly as given, and a distinct (non-
    // default) procFunc/finishFunc pair is recorded as such - matching
    // type 0/1's own distinct pairs (Sec9.3), never invoked by this port
    // (Sec10 OPEN 32/38 - see dispatch() calling nothing but the subtype
    // handler, tested below).
    std::vector<std::string> calls;
    std::vector<JobTypeRow::Handler> type2Table(kType2SubtypeCount);
    for (int i = 0; i < kType2SubtypeCount; ++i) {
        type2Table[i] = [&calls, i](const JobDescriptor&) { calls.push_back("type2:" + std::to_string(i)); };
    }
    table.registerJobType(2, type2Table, /*hasCustomProcFunc=*/true, /*hasCustomFinishFunc=*/true);

    CHECK(table.isRegistered(2));
    const JobTypeRow& row2 = table.rowFor(2);
    CHECK(row2.subtypeTableLength() == static_cast<size_t>(kType2SubtypeCount));
    CHECK(!row2.procFuncIsDefault());
    CHECK(!row2.finishFuncIsDefault());
}

void testJobDispatchRoutingAndFault() {
    JobTypeTable table;

    // Types 0 and 1 share one 23-slot table, every slot the SAME handler
    // (Sec9.3, CONFIRMED) - modelled directly: the subtype byte makes no
    // difference at this layer for these two types.
    std::vector<std::string> log;
    JobTypeRow::Handler sharedHandler = [&log](const JobDescriptor& d) {
        log.push_back("type" + std::to_string(d.type) + ":sub" + std::to_string(d.subtype));
    };
    std::vector<JobTypeRow::Handler> type01Table(kType01SubtypeCount, sharedHandler);
    table.registerJobType(0, type01Table);
    table.registerJobType(1, type01Table);

    JobDescriptor a{/*subtype=*/0, /*type=*/0};
    JobDescriptor b{/*subtype=*/22, /*type=*/1};  // last of the 23 slots
    table.dispatch(a);
    table.dispatch(b);
    CHECK(log.size() == 2);
    CHECK(log[0] == "type0:sub0");
    CHECK(log[1] == "type1:sub22");

    // "Not re-checked at dispatch time" (Sec9.0): a subtype PAST the
    // registered length (23) for a REGISTERED type still reaches
    // dispatch's own table lookup rather than being rejected up front by
    // subtypeTableLength - and, since that slot was never filled, it
    // faults exactly like a wholly unregistered type does (Sec9.2's "no
    // soft-fail path").
    JobDescriptor pastLength{/*subtype=*/23, /*type=*/0};
    bool threw = false;
    try {
        table.dispatch(pastLength);
    } catch (const JobDispatchFault& e) {
        threw = true;
        CHECK(e.type() == 0);
        CHECK(e.subtype() == 23);
    }
    CHECK(threw);

    // A wholly unregistered type (Sec9.2: NULL subtypeTable) faults the
    // same way.
    threw = false;
    JobDescriptor unregistered{/*subtype=*/0, /*type=*/9};
    try {
        table.dispatch(unregistered);
    } catch (const JobDispatchFault&) {
        threw = true;
    }
    CHECK(threw);
}

void testJobTypeTableSealDiscipline() {
    // "The whole job-type table is built exactly once, at physics
    // bring-up ... and never touched again anywhere else in the binary"
    // (Sec9.1, CONFIRMED) - seal() turns a second registerJobType() call
    // into a loud failure rather than a silent overwrite.
    JobTypeTable table;
    CHECK(!table.isSealed());
    table.registerJobType(0, std::vector<JobTypeRow::Handler>(kType01SubtypeCount));
    table.seal();
    CHECK(table.isSealed());

    bool threw = false;
    try {
        table.registerJobType(1, std::vector<JobTypeRow::Handler>(kType01SubtypeCount));
    } catch (const std::logic_error&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(!table.isRegistered(1));  // the rejected call never wrote the row
}

void testJobTypeCountOpenValue() {
    // Sec9.2 OPEN item 36: the constructor's own job-type-count argument
    // (cinfo+0x1c) was not pinned to a value this pass - exposed here as a
    // genuine OpenValue, refusing a read until set() rather than guessing.
    JobTypeTable table;
    CHECK(!table.bootstrapRowCount.known());
    bool threw = false;
    try {
        table.bootstrapRowCount.get();
    } catch (const sr3luahost::OpenStateError&) {
        threw = true;
    }
    CHECK(threw);

    table.bootstrapRowCount.set(32);
    CHECK(table.bootstrapRowCount.known());
    CHECK(table.bootstrapRowCount.get() == 32u);
}

void testJobQueueFifoDrainOrder() {
    // "getJob ... if no job left: break" (Sec9.0) - FIFO order, draining
    // to empty, returning the count actually dispatched.
    JobTypeTable table;
    std::vector<int> order;
    std::vector<JobTypeRow::Handler> type3Table(kType3SubtypeCount);
    for (int i = 0; i < kType3SubtypeCount; ++i) {
        type3Table[i] = [&order, i](const JobDescriptor&) { order.push_back(i); };
    }
    table.registerJobType(3, type3Table);

    JobQueueFifo fifo;
    fifo.push(JobDescriptor{0, 3});
    fifo.push(JobDescriptor{5, 3});
    fifo.push(JobDescriptor{2, 3});
    CHECK(fifo.pendingCount() == 3);

    size_t n = fifo.drainAll(table);
    CHECK(n == 3);
    CHECK(fifo.pendingCount() == 0);
    CHECK((order == std::vector<int>{0, 5, 2}));

    // Draining an already-empty queue dispatches nothing and does not
    // throw (there is simply no job left).
    CHECK(fifo.drainAll(table) == 0);
}

// IPhysicsBackend::drainJobs() (frame_contract.h) already carried the HIGH
// tag "0x00be88c0, hkJobQueue::processAllJobs" - JobQueuePhysicsBackend is
// that tag's CONFIRMED body (Sec9), wired behind the same interface so
// RunPhysicsStep (Sec4.4) can drive it exactly like NullPhysicsBackend,
// with no change to frame_contract.h itself.
void testJobQueuePhysicsBackendIntegration() {
    JobQueuePhysicsBackend backend;
    std::vector<std::string> log;
    std::vector<JobTypeRow::Handler> type2Table(kType2SubtypeCount);
    type2Table[0] = [&log](const JobDescriptor&) { log.push_back("type2:sub0"); };
    backend.jobTypes.registerJobType(2, type2Table);
    backend.jobTypes.seal();

    backend.pendingJobs.push(JobDescriptor{/*subtype=*/0, /*type=*/2});
    CHECK(backend.pendingJobs.pendingCount() == 1);

    InlineGameJobScheduler scheduler;
    RunPhysicsStep(backend, scheduler, 1.0 / 30.0);  // Sec4.4: begin -> 2 drains -> 1 drain -> finish -> wait

    // drainJobs() ran at least once with a job pending (the first of the
    // kStepJobCount inline-submitted jobs, or the calling-thread drain -
    // RunPhysicsStep does not guarantee which one sees it, only that it
    // runs before the step returns) and dispatched it exactly once; later
    // drains see an empty queue and dispatch nothing further.
    CHECK(log.size() == 1);
    CHECK(log[0] == "type2:sub0");
    CHECK(backend.pendingJobs.pendingCount() == 0);
}

// ===================================================================
// Bring-up / shutdown order (Sec4.1, Sec4.5 - CONFIRMED order for
// shutdown): game pool -> phantoms -> listeners -> job queue -> world ->
// utility -> per-thread memory -> base system -> clear guard byte. "The
// job queue is destroyed before the world."
// ===================================================================
void testLifecycleOrder() {
    PhysicsLifecycle sys;
    std::vector<std::string> log;
    auto rec = [&](const char* name) { return [&log, name] { log.push_back(name); }; };

    sys.bringUpMemoryAndBaseSystem = rec("mem+base");
    sys.bringUpWorld = rec("world");
    sys.bringUpJobQueue = rec("jobqueue");
    sys.bringUpBorderAndUtility = rec("border+utility");
    sys.bringUpBodyTables = rec("bodytables");
    sys.bringUpPerThreadMemoryAndPhantoms = rec("perthread+phantoms");

    sys.shutdownGamePool = rec("shutdown:pool");
    sys.shutdownPhantoms = rec("shutdown:phantoms");
    sys.shutdownListeners = rec("shutdown:listeners");
    sys.shutdownJobQueue = rec("shutdown:jobqueue");
    sys.shutdownWorld = rec("shutdown:world");
    sys.shutdownUtility = rec("shutdown:utility");
    sys.shutdownPerThreadMemory = rec("shutdown:perthread");
    sys.shutdownBaseSystem = rec("shutdown:base");

    // Shutdown before bring-up: a no-op (Sec4.5 preamble).
    sys.shutdown();
    CHECK(log.empty());
    CHECK(!sys.isBroughtUp());

    sys.bringUp();
    std::vector<std::string> expectedBringUp = {"mem+base", "world", "jobqueue", "border+utility",
                                                 "bodytables", "perthread+phantoms"};
    CHECK(log == expectedBringUp);
    CHECK(sys.isBroughtUp());

    // A second bring-up call refuses (Sec4.1 preamble): no hook re-runs.
    log.clear();
    sys.bringUp();
    CHECK(log.empty());

    log.clear();
    sys.shutdown();
    std::vector<std::string> expectedShutdown = {"shutdown:pool",      "shutdown:phantoms",
                                                  "shutdown:listeners", "shutdown:jobqueue",
                                                  "shutdown:world",     "shutdown:utility",
                                                  "shutdown:perthread", "shutdown:base"};
    CHECK(log == expectedShutdown);
    CHECK(!sys.isBroughtUp());
    // The job queue is destroyed strictly before the world.
    auto jobqueueIt = std::find(log.begin(), log.end(), std::string("shutdown:jobqueue"));
    auto worldIt = std::find(log.begin(), log.end(), std::string("shutdown:world"));
    CHECK(jobqueueIt < worldIt);

    // A second shutdown call is a no-op (the guard byte is already clear).
    log.clear();
    sys.shutdown();
    CHECK(log.empty());

    // Bring-up again after a clean shutdown runs fully again.
    log.clear();
    sys.bringUp();
    CHECK(log == expectedBringUp);
}

// ===================================================================
// The character-proxy lock-step pass cap (Sec4.6 item 3 / spec-player-
// runtime.md Sec5.6.4 step 3): never exceeds 10, stops early once a
// proxy's own remaining time is satisfied.
// ===================================================================
void testCharacterProxyPassCap() {
    CharacterProxyStepper stepper;

    // A proxy satisfied in exactly 3 passes (consumes all remaining time on
    // the 3rd): runs fewer than the cap.
    {
        int callCount = 0;
        auto solve = [&](double remaining) {
            ++callCount;
            return (callCount >= 3) ? remaining : remaining / 4.0;
        };
        const int passes = stepper.run(1.0, solve);
        CHECK(passes == 3);
    }

    // A proxy that never resolves (each pass consumes a tiny, fixed
    // fraction, never enough to cross the epsilon): capped at exactly 10,
    // never 11.
    {
        auto solve = [](double) { return 1e-6; };  // tiny progress every pass
        const int passes = stepper.run(1.0, solve);
        CHECK(passes == CharacterProxyStepper::kMaxPasses);
        CHECK(passes == 10);
    }

    // Zero-or-negative remaining time: already satisfied, gets no passes.
    {
        bool solveCalled = false;
        auto solve = [&](double) {
            solveCalled = true;
            return 0.0;
        };
        const int passes = stepper.run(0.0, solve);
        CHECK(passes == 0);
        CHECK(!solveCalled);
    }

    // runBatch(): the real lock-step shape - one proxy resolves on its very
    // first pass and drops out, a second never resolves; the batch runs
    // exactly as many shared passes as the SLOWER proxy needs (capped at
    // kMaxPasses), and the faster proxy stops accumulating passes once it
    // drops out. runBatch() calls the same SolvePass for every active proxy
    // each pass with only `remaining` as input (no proxy identity), so the
    // two proxies are given DIFFERENT starting remaining times and the
    // shared solve() tells them apart purely by value: "at or below 1 unit
    // of time" resolves fully at once, "above 1 unit" (the second proxy,
    // starting at 2.0 and only ever nudged down by a negligible amount)
    // never crosses that line within the capped 10 passes.
    {
        std::vector<CharacterProxyStepper::ProxyState> batch = {{1.0, 0}, {2.0, 0}};
        auto divergentSolve = [](double remaining) {
            if (remaining <= 1.0) return remaining;  // fully resolved in one pass
            return 1e-6;                              // otherwise: barely moves
        };
        const int passes = stepper.runBatch(batch, divergentSolve);
        CHECK(passes == CharacterProxyStepper::kMaxPasses);  // the slower proxy forces the full cap
        CHECK(batch[0].passesRun == 1);                       // dropped out after resolving once
        CHECK(batch[1].passesRun == CharacterProxyStepper::kMaxPasses);  // ran every pass, still unresolved
        CHECK(batch[0].remaining <= CharacterProxyStepper::kRemainingTimeEpsilon);
        CHECK(batch[1].remaining > CharacterProxyStepper::kRemainingTimeEpsilon);
    }
}

// ===================================================================
// MyCharacterListener's queue-only contract (Sec2.2 / spec-player-
// runtime.md Sec5.8): callbacks can only enqueue, never mutate game state
// synchronously; duplicate (human, object) pairs merge by running
// average; each queue caps at 100 entries.
// ===================================================================
struct TestInteractionRecord {
    int human = 0;
    int object = 0;
    double value = 0.0;
    int mergeCount = 1;
};

void testListenerQueueOnlyContract() {
    auto keyEq = [](const TestInteractionRecord& a, const TestInteractionRecord& b) {
        return a.human == b.human && a.object == b.object;
    };
    auto merge = [](TestInteractionRecord& existing, const TestInteractionRecord& incoming) {
        // A running average: existing <- existing + (incoming - existing) / (n+1).
        ++existing.mergeCount;
        existing.value += (incoming.value - existing.value) / existing.mergeCount;
    };

    CharacterProxyListener<TestInteractionRecord, TestInteractionRecord> listener(keyEq, merge, keyEq, merge);

    // The structural part of the contract: this callback takes ONLY the
    // EnqueueOnly<T>& the dispatcher hands it - no captured game state at
    // all - so by construction it cannot mutate anything but the queue.
    listener.onObjectInteraction = [](EnqueueOnly<TestInteractionRecord>& q) {
        q.push(TestInteractionRecord{1, 2, 10.0, 1});
    };
    listener.fireObjectInteraction();
    CHECK(listener.objectQueueSize() == 1);

    // A duplicate (human, object) pair merges by running average rather
    // than appending a second entry.
    listener.onObjectInteraction = [](EnqueueOnly<TestInteractionRecord>& q) {
        q.push(TestInteractionRecord{1, 2, 20.0, 1});
    };
    listener.fireObjectInteraction();
    CHECK(listener.objectQueueSize() == 1);  // still one entry: merged, not appended

    // A different pair appends a second, distinct entry.
    listener.onObjectInteraction = [](EnqueueOnly<TestInteractionRecord>& q) {
        q.push(TestInteractionRecord{3, 4, 5.0, 1});
    };
    listener.fireObjectInteraction();
    CHECK(listener.objectQueueSize() == 2);

    // characterInteractionCallback feeds the OTHER queue.
    listener.onCharacterInteraction = [](EnqueueOnly<TestInteractionRecord>& q) {
        q.push(TestInteractionRecord{5, 6, 1.0, 1});
    };
    listener.fireCharacterInteraction();
    CHECK(listener.characterQueueSize() == 1);
    CHECK(listener.objectQueueSize() == 2);  // unaffected - the two queues are independent

    // processConstraintsCallback is handed BOTH views and may push into
    // either (spec does not establish which, if any).
    listener.onProcessConstraints = [](EnqueueOnly<TestInteractionRecord>& objQ,
                                        EnqueueOnly<TestInteractionRecord>&) {
        objQ.push(TestInteractionRecord{7, 8, 2.0, 1});
    };
    listener.fireProcessConstraints();
    CHECK(listener.objectQueueSize() == 3);

    // The 100-entry cap: pushing 200 distinct pairs never exceeds 100.
    CharacterProxyListener<TestInteractionRecord, TestInteractionRecord> capListener(keyEq, merge, keyEq, merge);
    for (int i = 0; i < 200; ++i) {
        capListener.onObjectInteraction = [i](EnqueueOnly<TestInteractionRecord>& q) {
            q.push(TestInteractionRecord{i, i, double(i), 1});
        };
        capListener.fireObjectInteraction();
    }
    CHECK(capListener.objectQueueSize() == QueueOnlyRecordQueue<TestInteractionRecord>::kMaxEntries);
    CHECK(capListener.objectQueueSize() == 100);

    // Flush: ordinary game code reads and clears, outside any pass. A
    // single flush() call drains and clears BOTH queues.
    std::vector<TestInteractionRecord> flushedObjects;
    std::vector<TestInteractionRecord> flushedCharacters;
    listener.flush([&](const TestInteractionRecord& r) { flushedObjects.push_back(r); },
                    [&](const TestInteractionRecord& r) { flushedCharacters.push_back(r); });
    CHECK(flushedObjects.size() == 3);
    CHECK(flushedCharacters.size() == 1);
    CHECK(listener.objectQueueSize() == 0);
    CHECK(listener.characterQueueSize() == 0);
}

}  // namespace

int main() {
    testDtClamp();
    testPhysicsWindow();
    testActivationQueueCategory();
    testBodyStateQueueCategory();
    testDroppedSetterCategory();
    testPhysicsStepJobShape();
    testFrameDriverFullFrame();
    testJobTypeRowStorageContract();
    testJobDispatchRoutingAndFault();
    testJobTypeTableSealDiscipline();
    testJobTypeCountOpenValue();
    testJobQueueFifoDrainOrder();
    testJobQueuePhysicsBackendIntegration();
    testLifecycleOrder();
    testCharacterProxyPassCap();
    testListenerQueueOnlyContract();
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_physics_runtime_test: all checks passed\n";
    return 0;
}
