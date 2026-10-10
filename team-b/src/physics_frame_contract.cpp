// sr3physics/frame_contract.h implementation. spec-physics-runtime.md
// Sec4.3/Sec4.4 - see the header's own doc comments for citations.
#include "sr3physics/frame_contract.h"

namespace sr3physics {

double ClampDt(double rawFrameDt, double timeScale) {
    double clamped = rawFrameDt;
    if (clamped < kMinDt) clamped = kMinDt;
    if (clamped > kMaxDt) clamped = kMaxDt;
    if (timeScale != 0.0) clamped /= timeScale;
    return clamped;
}

void RunPhysicsStep(IPhysicsBackend& backend, IGameJobScheduler& scheduler, double dt) {
    // 1. Begin, into Havok (Sec4.4 step 1).
    backend.beginStep(dt);

    // 2-3. Describe a game job, open a job group, submit kStepJobCount jobs
    // on the game's own scheduler - each one drains the same Havok job
    // queue (Sec4.4 steps 2-3 / "the worker job 0x00754dd0").
    for (int i = 0; i < kStepJobCount; ++i) {
        scheduler.submitJob([&backend] { backend.drainJobs(); });
    }

    // 4. Drain on this thread too (Sec4.4 step 4).
    backend.drainJobs();

    // 5. Finish, into Havok, with a null thread pool deliberately (Sec4.4 step 5).
    backend.finishStep();

    // 6. Wait for the 2 game jobs to complete (Sec4.4 step 6).
    scheduler.waitAll();
}

PhysicsFrameDriver::PhysicsFrameDriver(IPhysicsBackend& backend, IGameJobScheduler& scheduler)
    : backend_(backend), scheduler_(scheduler) {}

void PhysicsFrameDriver::requestActivation(BodyHandle body, bool activate) {
    if (window.isOpen()) {
        activationQueue.enqueue(body, activate);
    } else if (applyActivation) {
        applyActivation(body, activate);
    }
}

void PhysicsFrameDriver::requestBodyStateChange(BodyHandle body, BodyStateChange change) {
    if (window.isOpen()) {
        bodyStateQueue.enqueue(body, std::move(change));
    } else if (applyBodyStateChange) {
        applyBodyStateChange(body, change);
    }
}

double PhysicsFrameDriver::runFrame(double rawFrameDt, double timeScale) {
    const double dt = ClampDt(rawFrameDt, timeScale);

    window.enter();  // Sec4.3 step 2: InterlockedIncrement(0x0161d394)

    if (waterPreStep) waterPreStep(dt);      // Sec4.3 step 3
    if (ragdollPreStep) ragdollPreStep(dt);  // Sec4.3 step 3

    if (vehiclePreStep) vehiclePreStep(dt);  // Sec4.3 step 4, exactly once

    RunPhysicsStep(backend_, scheduler_, dt);  // Sec4.4, the step itself

    if (handOffToGameplay) handOffToGameplay(dt);  // Sec4.3 step 5

    // The driver's own internal replay - bypasses requestActivation() /
    // requestBodyStateChange() entirely (see runFrame()'s own design note
    // in the header for why).
    if (applyActivation) {
        activationQueue.replay(applyActivation);
    }
    if (applyBodyStateChange && queryIsActive) {
        bodyStateQueue.replay(applyBodyStateChange, queryIsActive);
    }

    if (dispatchContactEvents) dispatchContactEvents();  // Sec4.3 step 5

    window.leave();  // Sec4.3 step 6: InterlockedDecrement(0x0161d394)

    postWindowQueue.flush();  // Sec4.3 step 7

    if (periodicMemoryUpkeep) periodicMemoryUpkeep();  // Sec4.3 step 7

    return dt;
}

}  // namespace sr3physics
