// sr3audioruntime - Wwise-internal mechanics (U-W1/U-W2a/U-W2b): see
// include/sr3audioruntime/wwise_internal.h for the contract this
// implements (spec-audio-runtime.md Sec7/Sec8/Sec9).

#include "sr3audioruntime/wwise_internal.h"

namespace sr3audioruntime::wwise_internal {

// =====================================================================
// Sec8 (U-W1): HIRC load dispatch
// =====================================================================

HircDispatchResult HircRegistry::dispatchItem(const HircItemDesc& item) {
    // Sec8.2.1: `ecx = kind - 1; cmp ecx, 0x12; ja fallback` - out of
    // [1,19] goes to the same shared fallback as kinds 10-13.
    if (!IsValidHircKind(item.kind) || IsUnregisteredFallbackKind(item.kind)) {
        if (unregisteredCallback_) unregisteredCallback_(item.kind, item.id);
        return HircDispatchResult::kUnregisteredFallback;
    }
    auto& ids = registered_[item.kind];
    if (ids.count(item.id) != 0) {
        // Sec8.2.3: "else: already registered - re-applying/updating" ->
        // call <kind-specific "apply fields" function>(obj, ...).
        return HircDispatchResult::kAppliedUpdate;
    }
    // Sec8.2.3: "if (obj == 0): first time this id has been seen" ->
    // allocate new object.
    ids.insert(item.id);
    return HircDispatchResult::kAllocatedNew;
}

bool HircRegistry::contains(HircKind kind, uint32_t id) const {
    auto it = registered_.find(kind);
    if (it == registered_.end()) return false;
    return it->second.count(id) != 0;
}

size_t HircRegistry::registeredCount(HircKind kind) const {
    auto it = registered_.find(kind);
    return it == registered_.end() ? 0 : it->second.size();
}

// =====================================================================
// Sec9 (U-W2a): Event lookup + deferred post-job enqueue
// =====================================================================

bool EventRegistry::postEvent(uint32_t eventId, int64_t gameObjectId, uint32_t scratchSequence, uint32_t uFlags,
                               bool hasCallback) {
    // Sec9.2.1 step 2: "On a miss (eax==0), returns 0 (AK_INVALID_PLAYING_ID)
    // - no further Wwise work at all."
    if (!contains(eventId)) return false;

    DeferredPostJob job;
    job.eventId = eventId;              // +0x08
    job.gameObjectId = gameObjectId;     // +0x0c
    job.scratchSequence = scratchSequence;  // +0x20
    job.hasCallback = hasCallback;
    // Sec9.3: "+0x30 = uFlags, masked to 0xffff0004 when pfnCallback == 0
    // (suppresses most notification bits when nobody can receive them)".
    job.uFlags = hasCallback ? uFlags : (uFlags & 0xffff0004u);
    jobs_.push_back(job);
    return true;
}

// =====================================================================
// Sec9.4 (U-W2a): the Action-object family
// =====================================================================

ActionObject& ActionRegistry::createOrGet(uint32_t id) {
    auto it = actions_.find(id);
    if (it != actions_.end()) {
        // Sec9.4.4: a second lookup of an already-registered object is an
        // AddRef, not a fresh construction.
        it->second.addRef();
        return it->second;
    }
    ActionObject obj;
    obj.id = id;
    auto result = actions_.emplace(id, obj);
    return result.first->second;
}

ActionObject* ActionRegistry::find(uint32_t id) {
    auto it = actions_.find(id);
    return it == actions_.end() ? nullptr : &it->second;
}

void ActionRegistry::release(uint32_t id) {
    auto it = actions_.find(id);
    if (it == actions_.end()) return;
    // Sec9.4.1 slot 2: "dec [this+4]; at 0, removes self from the hash
    // table... calls the dtor... frees via the shared deallocator."
    if (it->second.release()) actions_.erase(it);
}

// =====================================================================
// Sec7 (U-W2b): container-type dispatch + voice instantiation
// =====================================================================

VoiceInstance* VoiceInstancePool::instantiate(uint32_t candidateId, float priority) {
    if (instances_.size() >= capacityLimit_) {
        // Sec7.3.3: "if at/over capacity: scans the vector for the entry
        // with the lowest priority (float at entry+0x1c) and, if one is
        // found, [evicts it]." capacityLimit_ == 0 (or an otherwise empty
        // pool already at its limit) means nothing CAN be found.
        if (instances_.empty()) return nullptr;
        size_t victimIdx = 0;
        for (size_t i = 1; i < instances_.size(); ++i) {
            if (instances_[i].priority < instances_[victimIdx].priority) victimIdx = i;
        }
        instances_.erase(instances_.begin() + static_cast<std::ptrdiff_t>(victimIdx));
    }
    instances_.push_back(VoiceInstance{candidateId, priority});
    return &instances_.back();
}

ContainerDispatchOutcome ContainerDispatchRoot::dispatch(const ContainerCandidate& candidate) {
    // Sec7.1 step 3: the "may this proceed" precondition gate
    // (vtable[ebp+0x7c]). False -> tail block, no child ever chosen.
    if (!candidate.proceedPrecondition) {
        return ContainerDispatchOutcome::kAbandonedPreconditionFailed;
    }
    // Sec7.1 step 5's flags test only decides WHICH "pick a child"
    // strategy runs (the fast path 0x00f51cc0 vs. the generic polymorphic
    // vtable[+0x10] path) - neither strategy's own internals are modelled
    // here (Sec7.6), so it is recorded for field-value parity only and has
    // no branch of its own in this function.
    (void)candidate.fastPathEligible;
    // Sec7.1 step 6: edi == 0 -> no eligible child was chosen by whichever
    // path ran; tail block, request abandoned cleanly.
    if (!candidate.childChosen) {
        return ContainerDispatchOutcome::kAbandonedNoEligibleChild;
    }
    // Sec7.1 step 7: the chosen child's own re-check (vtable[edi+8]).
    // False -> the chosen child is freed without ever reaching voice
    // instantiation.
    if (!candidate.childEligible) {
        return ContainerDispatchOutcome::kAbandonedChildRecheckFailed;
    }
    // Sec7.1 step 7 success -> Sec7.3's voice-instantiation hand-off
    // (0x00f51e50 -> 0x00f4fc30 -> 0x00f53c40).
    VoiceInstance* instance = pool_.instantiate(candidate.id, candidate.priority);
    if (!instance) return ContainerDispatchOutcome::kInstantiationFailed;
    return ContainerDispatchOutcome::kInstantiated;
}

}  // namespace sr3audioruntime::wwise_internal
