// Synthetic tests for the Wwise-internal mechanics past the WwiseBackend
// boundary (spec-audio-runtime.md Sec7/Sec8/Sec9, units U-W1/U-W2a/U-W2b).
// Every test cites the spec section it exercises. This is a SEPARATE test
// binary from synthetic_audio_runtime_test (which pins down the GAME-side
// Sec1-6 contract); nothing here touches AudioRuntime or WwiseBackend.
#include <iostream>
#include <string>
#include <vector>

#include "sr3audioruntime/wwise_internal.h"
#include "sr3luahost/open_state.h"

using namespace sr3audioruntime::wwise_internal;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                      \
    } while (0)

template <typename F>
bool throwsOpenStateError(F&& f, const char* needle = nullptr) {
    try {
        f();
    } catch (const sr3luahost::OpenStateError& e) {
        if (needle != nullptr && e.spec().find(needle) == std::string::npos &&
            e.global().find(needle) == std::string::npos) {
            std::cerr << "  (OPEN thrown but didn't name '" << needle << "': " << e.what() << ")\n";
            return false;
        }
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------
// Sec8.2.1/8.2.2 (U-W1): the 19-entry kind dispatch, kinds 10-13 always
// fall back, and the find-or-create allocate-vs-apply split.
// ---------------------------------------------------------------------
void testHircKindDispatch_Kinds10To13AlwaysFallback() {
    HircRegistry reg;
    for (HircKind k = 10; k <= 13; ++k) {
        CHECK(reg.dispatchItem(HircItemDesc{k, 42}) == HircDispatchResult::kUnregisteredFallback);
    }
    CHECK(reg.registeredCount(10) == 0);
    CHECK(reg.registeredCount(11) == 0);
}

void testHircKindDispatch_OutOfRangeFallsBack() {
    HircRegistry reg;
    CHECK(reg.dispatchItem(HircItemDesc{0, 1}) == HircDispatchResult::kUnregisteredFallback);   // below kHircKindMin
    CHECK(reg.dispatchItem(HircItemDesc{20, 1}) == HircDispatchResult::kUnregisteredFallback);  // above kHircKindMax (19)
}

void testHircKindDispatch_FirstSeenAllocatesThenApplies() {
    HircRegistry reg;
    CHECK(reg.dispatchItem(HircItemDesc{1, 100}) == HircDispatchResult::kAllocatedNew);  // Sec8.2.3: first time
    CHECK(reg.contains(1, 100));
    CHECK(reg.registeredCount(1) == 1);
    // Same (kind,id) again: already registered -> apply/update path, not a second allocation.
    CHECK(reg.dispatchItem(HircItemDesc{1, 100}) == HircDispatchResult::kAppliedUpdate);
    CHECK(reg.registeredCount(1) == 1);  // still exactly one registered id for kind 1

    // A different kind gets its own namespace (this project's own
    // finest-grained-partition judgment call, Sec8.2.3's OPEN grouping -
    // see wwise_internal.h's HircRegistry doc comment).
    CHECK(reg.dispatchItem(HircItemDesc{2, 100}) == HircDispatchResult::kAllocatedNew);
    CHECK(reg.registeredCount(2) == 1);
}

void testHircRegistry_UnregisteredCallbackInvokedWithKindAndId() {
    HircRegistry reg;
    std::vector<std::pair<HircKind, uint32_t>> seen;
    reg.setUnregisteredCallback([&](HircKind k, uint32_t id) { seen.push_back({k, id}); });
    reg.dispatchItem(HircItemDesc{11, 777});
    CHECK(seen.size() == 1);
    CHECK(seen[0].first == 11);
    CHECK(seen[0].second == 777);
    // A valid, non-fallback kind must NOT invoke the callback.
    reg.dispatchItem(HircItemDesc{5, 1});
    CHECK(seen.size() == 1);
}

// ---------------------------------------------------------------------
// Sec9.2.1 step 2 / Sec9.2.2 (U-W2a): Event-id lookup miss vs hit.
// ---------------------------------------------------------------------
void testEventRegistry_UnknownEventIdPostsNoJob() {
    EventRegistry events;
    bool ok = events.postEvent(/*eventId=*/9999, /*gameObjectId=*/1, /*scratchSequence=*/0, /*uFlags=*/0,
                                /*hasCallback=*/false);
    CHECK(!ok);  // Sec9.2.1 step 2: miss -> AK_INVALID_PLAYING_ID, no further work
    CHECK(events.queuedJobCount() == 0);
}

void testEventRegistry_KnownEventEnqueuesJob() {
    EventRegistry events;
    events.registerEvent(42);
    bool ok = events.postEvent(42, /*gameObjectId=*/7, /*scratchSequence=*/0x55, /*uFlags=*/0x1234,
                                /*hasCallback=*/true);
    CHECK(ok);
    CHECK(events.queuedJobCount() == 1);
    const DeferredPostJob& job = events.queuedJobs().front();
    CHECK(job.eventId == 42);
    CHECK(job.gameObjectId == 7);
    CHECK(job.scratchSequence == 0x55);
    CHECK(job.uFlags == 0x1234);  // hasCallback=true: not masked
    CHECK(job.hasCallback);
}

void testEventRegistry_NoCallbackMasksFlagsPerSec9_3() {
    EventRegistry events;
    events.registerEvent(1);
    // Sec9.3: "+0x30 = uFlags, masked to 0xffff0004 when pfnCallback == 0".
    events.postEvent(1, 0, 0, /*uFlags=*/0xffffffffu, /*hasCallback=*/false);
    CHECK(events.queuedJobs().back().uFlags == 0xffff0004u);
}

void testEventRegistry_DrainIsOpenStateError() {
    EventRegistry events;
    CHECK(throwsOpenStateError([&] { events.drainQueuedJobs(); }, "Sec9.6"));
}

// ---------------------------------------------------------------------
// Sec9.2.2 / Sec7.5.1 (U-W2a): the CONFIRMED 193 divisor.
// ---------------------------------------------------------------------
void testHashBucket193_ConfirmedDivisor() {
    CHECK(HashBucket193(0) == 0);
    CHECK(HashBucket193(193) == 0);
    CHECK(HashBucket193(194) == 1);
    CHECK(HashBucket193(5) == 5);
    CHECK(kRegistryBucketCount == 193);
}

// ---------------------------------------------------------------------
// Sec9.4.1/9.4.4 (U-W2a): Action-object AddRef/Release registry shape.
// ---------------------------------------------------------------------
void testActionRegistry_CreateOrGetAddRefsOnSecondLookup() {
    ActionRegistry actions;
    ActionObject& a1 = actions.createOrGet(10);
    CHECK(a1.refCount() == 1);  // Sec9.4.1 ctor: live with one reference as soon as it's inserted
    ActionObject& a2 = actions.createOrGet(10);
    CHECK(&a1 == &a2);
    CHECK(a2.refCount() == 2);  // second lookup is an AddRef, not a fresh object
    CHECK(actions.count() == 1);
}

void testActionRegistry_ReleaseToZeroRemovesFromRegistry() {
    ActionRegistry actions;
    actions.createOrGet(5);       // refcount 1
    actions.createOrGet(5);       // refcount 2 (AddRef)
    actions.release(5);           // -> 1, still present
    CHECK(actions.find(5) != nullptr);
    actions.release(5);           // -> 0, removed (Sec9.4.1 slot 2)
    CHECK(actions.find(5) == nullptr);
    CHECK(actions.count() == 0);
}

void testActionRegistry_TargetSubObjectIsOpaqueCallerSet() {
    ActionRegistry actions;
    ActionObject& a = actions.createOrGet(1);
    CHECK(a.targetSubObject == 0);  // Sec9.4.2's +0x68 field: opaque until the caller sets it
    a.targetSubObject = 0xAABBCCDD;
    CHECK(actions.find(1)->targetSubObject == 0xAABBCCDD);
}

// ---------------------------------------------------------------------
// Sec7.1 (U-W2b): the container-dispatch root's 4+1 outcomes.
// ---------------------------------------------------------------------
void testContainerDispatch_PreconditionFailedAbandonsBeforeAnyChild() {
    VoiceInstancePool pool(4);
    ContainerDispatchRoot root(pool);
    ContainerCandidate c;
    c.proceedPrecondition = false;  // Sec7.1 step 3 gate fails
    CHECK(root.dispatch(c) == ContainerDispatchOutcome::kAbandonedPreconditionFailed);
    CHECK(pool.size() == 0);
}

void testContainerDispatch_NoEligibleChildAbandons() {
    VoiceInstancePool pool(4);
    ContainerDispatchRoot root(pool);
    ContainerCandidate c;
    c.childChosen = false;  // Sec7.1 step 6: edi == 0
    CHECK(root.dispatch(c) == ContainerDispatchOutcome::kAbandonedNoEligibleChild);
    CHECK(pool.size() == 0);
}

void testContainerDispatch_ChildRecheckFailedAbandons() {
    VoiceInstancePool pool(4);
    ContainerDispatchRoot root(pool);
    ContainerCandidate c;
    c.childChosen = true;
    c.childEligible = false;  // Sec7.1 step 7's own re-check fails
    CHECK(root.dispatch(c) == ContainerDispatchOutcome::kAbandonedChildRecheckFailed);
    CHECK(pool.size() == 0);
}

void testContainerDispatch_FullSuccessInstantiatesVoice() {
    VoiceInstancePool pool(4);
    ContainerDispatchRoot root(pool);
    ContainerCandidate c;
    c.id = 555;
    c.priority = 2.5f;
    c.proceedPrecondition = true;
    c.childChosen = true;
    c.childEligible = true;
    CHECK(root.dispatch(c) == ContainerDispatchOutcome::kInstantiated);
    CHECK(pool.size() == 1);
    CHECK(pool.instances()[0].candidateId == 555);
    CHECK(pool.instances()[0].priority == 2.5f);
}

void testContainerDispatch_InstantiationFailureAtZeroCapacity() {
    VoiceInstancePool pool(0);
    ContainerDispatchRoot root(pool);
    ContainerCandidate c;  // all gates default true/true
    CHECK(root.dispatch(c) == ContainerDispatchOutcome::kInstantiationFailed);
    CHECK(pool.size() == 0);
}

// ---------------------------------------------------------------------
// Sec7.3.3 (U-W2b): voice-slot pool capacity + lowest-priority eviction.
// ---------------------------------------------------------------------
void testVoicePool_UnderCapacityAlwaysAllocatesFresh() {
    VoiceInstancePool pool(2);
    CHECK(pool.instantiate(1, 1.0f) != nullptr);
    CHECK(pool.instantiate(2, 1.0f) != nullptr);
    CHECK(pool.size() == 2);
}

void testVoicePool_AtCapacityEvictsLowestPriority() {
    VoiceInstancePool pool(2);
    pool.instantiate(1, 5.0f);   // higher priority, should survive
    pool.instantiate(2, 1.0f);   // lowest priority, should be evicted
    CHECK(pool.size() == 2);

    VoiceInstance* third = pool.instantiate(3, 10.0f);
    CHECK(third != nullptr);
    CHECK(pool.size() == 2);  // capacity held, not grown

    bool sawId1 = false, sawId2 = false, sawId3 = false;
    for (const auto& inst : pool.instances()) {
        if (inst.candidateId == 1) sawId1 = true;
        if (inst.candidateId == 2) sawId2 = true;
        if (inst.candidateId == 3) sawId3 = true;
    }
    CHECK(sawId1);   // survived (higher priority than the victim)
    CHECK(!sawId2);  // evicted (lowest priority, Sec7.3.3)
    CHECK(sawId3);   // the new instance that triggered the eviction
}

void testVoicePool_ZeroCapacityNeverAllocates() {
    VoiceInstancePool pool(0);
    CHECK(pool.instantiate(1, 100.0f) == nullptr);  // Sec7.3.3: "if one is found" guard fails - nothing to evict
    CHECK(pool.size() == 0);
}

// ---------------------------------------------------------------------
// Sec9.4.2 (U-W2a/U-W2b seam): ApplyAction's confirmed, unconditional
// hand-off into the container-dispatch root.
// ---------------------------------------------------------------------
void testApplyAction_HandsOffIntoContainerDispatch() {
    VoiceInstancePool pool(4);
    ContainerDispatchRoot root(pool);
    ActionRegistry actions;
    ActionObject& action = actions.createOrGet(1);
    action.targetSubObject = 0x1000;

    ContainerCandidate resolved;
    resolved.id = 0x1000;
    resolved.priority = 1.0f;
    CHECK(ApplyAction(action, root, resolved) == ContainerDispatchOutcome::kInstantiated);
    CHECK(pool.size() == 1);
}

}  // namespace

int main() {
    const auto run = [](const char* name, void (*fn)()) {
        try {
            fn();
        } catch (const std::exception& e) {
            std::cerr << name << ": unexpected exception: " << e.what() << "\n";
            ++g_failures;
        }
    };

    run("testHircKindDispatch_Kinds10To13AlwaysFallback", testHircKindDispatch_Kinds10To13AlwaysFallback);
    run("testHircKindDispatch_OutOfRangeFallsBack", testHircKindDispatch_OutOfRangeFallsBack);
    run("testHircKindDispatch_FirstSeenAllocatesThenApplies", testHircKindDispatch_FirstSeenAllocatesThenApplies);
    run("testHircRegistry_UnregisteredCallbackInvokedWithKindAndId",
        testHircRegistry_UnregisteredCallbackInvokedWithKindAndId);

    run("testEventRegistry_UnknownEventIdPostsNoJob", testEventRegistry_UnknownEventIdPostsNoJob);
    run("testEventRegistry_KnownEventEnqueuesJob", testEventRegistry_KnownEventEnqueuesJob);
    run("testEventRegistry_NoCallbackMasksFlagsPerSec9_3", testEventRegistry_NoCallbackMasksFlagsPerSec9_3);
    run("testEventRegistry_DrainIsOpenStateError", testEventRegistry_DrainIsOpenStateError);
    run("testHashBucket193_ConfirmedDivisor", testHashBucket193_ConfirmedDivisor);

    run("testActionRegistry_CreateOrGetAddRefsOnSecondLookup", testActionRegistry_CreateOrGetAddRefsOnSecondLookup);
    run("testActionRegistry_ReleaseToZeroRemovesFromRegistry", testActionRegistry_ReleaseToZeroRemovesFromRegistry);
    run("testActionRegistry_TargetSubObjectIsOpaqueCallerSet", testActionRegistry_TargetSubObjectIsOpaqueCallerSet);

    run("testContainerDispatch_PreconditionFailedAbandonsBeforeAnyChild",
        testContainerDispatch_PreconditionFailedAbandonsBeforeAnyChild);
    run("testContainerDispatch_NoEligibleChildAbandons", testContainerDispatch_NoEligibleChildAbandons);
    run("testContainerDispatch_ChildRecheckFailedAbandons", testContainerDispatch_ChildRecheckFailedAbandons);
    run("testContainerDispatch_FullSuccessInstantiatesVoice", testContainerDispatch_FullSuccessInstantiatesVoice);
    run("testContainerDispatch_InstantiationFailureAtZeroCapacity",
        testContainerDispatch_InstantiationFailureAtZeroCapacity);

    run("testVoicePool_UnderCapacityAlwaysAllocatesFresh", testVoicePool_UnderCapacityAlwaysAllocatesFresh);
    run("testVoicePool_AtCapacityEvictsLowestPriority", testVoicePool_AtCapacityEvictsLowestPriority);
    run("testVoicePool_ZeroCapacityNeverAllocates", testVoicePool_ZeroCapacityNeverAllocates);

    run("testApplyAction_HandsOffIntoContainerDispatch", testApplyAction_HandsOffIntoContainerDispatch);

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_wwise_internal_test: all checks passed\n";
    return 0;
}
