#include "sr3audioruntime/global_rtpc.h"

#include <cmath>

namespace sr3audioruntime {

int32_t GlobalRtpcCache::findOrInsert(uint32_t id) {
    size_t start = id % kCapacity;
    for (size_t i = 0; i < kCapacity; ++i) {
        size_t idx = (start + i) % kCapacity;
        if (!slots_[idx].used) {
            slots_[idx].used = true;
            slots_[idx].id = id;
            // Caller fills in the value on a fresh insert.
            return static_cast<int32_t>(idx);
        }
        if (slots_[idx].id == id) return static_cast<int32_t>(idx);
    }
    return -1;  // table full of OTHER ids: the "0xff" sentinel (Sec1.3 behaviour #7)
}

bool GlobalRtpcCache::trySend(WwiseBackend& backend, uint32_t id, float value, bool engineUp) {
    if (!engineUp) return false;  // engine-down guard precedes the cache consult (Sec1.3)
    int32_t idx = findOrInsert(id);
    if (idx < 0) return false;  // full table -> "unchanged", not sent (behaviour #7)
    Slot& slot = slots_[static_cast<size_t>(idx)];
    bool changed = !slot.valueSet || std::fabs(value - slot.value) > kChangeEpsilon;
    slot.valueSet = true;
    if (!changed) return false;  // behaviour #7: sub-epsilon delta dropped
    slot.value = value;
    return backend.setRTPCValue(id, value, kGlobalObject);
}

bool SmoothedGlobalRtpc::tick(GlobalRtpcCache& cache, WwiseBackend& backend, bool engineUp) {
    // Sec1.3 0x0045f690 / Sec5 behaviour #6.
    if (current - target > kThreshold) {
        current = (current - target) * k_.get();  // OPEN item 7 if k was never set
    } else if (current != target) {
        current = target;
    } else {
        return false;  // unchanged, nothing to send
    }
    return cache.trySend(backend, kRtpcId, current * 100.0f, engineUp);
}

void MixRtpcBank::tick(GlobalRtpcCache& cache, WwiseBackend& backend, bool engineUp) {
    if (!live) {
        for (size_t i = 0; i < kCount; ++i) cache.trySend(backend, ids[i], 0.0f, engineUp);
        sentOnceSinceLive = false;
        return;
    }
    if (!sentOnceSinceLive) {
        for (size_t i = 0; i < kCount; ++i) {
            cache.trySend(backend, ids[i], values[i], engineUp);
            dirty[i] = false;
        }
        sentOnceSinceLive = true;
        return;
    }
    for (size_t i = 0; i < kCount; ++i) {
        if (dirty[i]) {
            cache.trySend(backend, ids[i], values[i], engineUp);
            dirty[i] = false;
        }
    }
}

bool SingleValueRtpc::tick(GlobalRtpcCache& cache, WwiseBackend& backend, bool engineUp) {
    int32_t value = sourceValue_.get();  // throws OpenStateError if never set (Sec6 item 7)
    bool changed = value != shadow_;
    shadow_ = value;  // "updated even when the engine is down" (Sec1.3)
    if (!changed) return false;
    return cache.trySend(backend, kRtpcId, static_cast<float>(value), engineUp);
}

bool ListenerState::tick(WwiseBackend& backend, bool engineUp) {
    if (armCounter <= 0) return false;
    const ListenerPosition& pos = position_.get();  // throws OpenStateError if never set (Sec6 item 7)
    bool sent = false;
    if (engineUp) sent = backend.setListenerPosition(pos, 0);
    --armCounter;  // decremented even when the engine is down (Sec1.3)
    return sent;
}

}  // namespace sr3audioruntime
