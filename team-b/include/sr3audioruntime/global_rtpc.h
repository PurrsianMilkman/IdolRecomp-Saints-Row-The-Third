#pragma once

// sr3audioruntime - the global RTPC path (spec-audio-runtime.md Sec1.3,
// Sec3.1 step 2/3, Sec3.3, Sec5 behaviours #6/#7/#8, Sec6 OPEN items 3/4/7/8).
//
// Only the GLOBAL SetRTPCValue crossings (object == -1) go through the
// 200-slot value cache; per-object RTPCs always call straight through
// (Sec0 item 3, Sec1.1 row 0x0045d1e0/0x0045d353). This header models:
//   - GlobalRtpcCache: the cache itself (behaviour #7).
//   - SmoothedGlobalRtpc: RTPC 0xdb2d51a3's asymmetric decay/snap (#6). Its
//     decay constant k (0x0317297c) is Sec6 OPEN item 7 - an OpenValue a
//     test must set before the decay branch is exercised; the growth/equal
//     branches never read it.
//   - MixRtpcBank: the seven mix RTPCs (ids/defaults Sec3.3, mode-byte
//     behaviour Sec1.3's three-branch rule).
//   - ListenerState: Sec1.3's 9-float listener position and its arm
//     counter (behaviour #8). The POSITION value is Sec6 OPEN (no spec
//     text gives it an initial value, unlike the smoothed-RTPC pair); the
//     arm counter is plain runtime gate state (starts at 0 = unarmed, the
//     only sane neutral element, not a guessed engine value) - see the
//     header comment on GlobalRtpcState below for this project's judgment
//     call.
//   - the single-value RTPC 0x9e3669da's source integer (Sec6 OPEN item 7
//     covers "who writes" it; no spec text gives an initial value either,
//     so it is also an OpenValue).

#include <array>
#include <cstdint>

#include "sr3audioruntime/types.h"
#include "sr3audioruntime/wwise_backend.h"
#include "sr3luahost/open_state.h"

namespace sr3audioruntime {

// Sec1.3 0x00470830: hash on id mod 200, no eviction. A change smaller than
// the epsilon is dropped; a full table drops the send entirely (behaviour
// #7).
class GlobalRtpcCache {
public:
    static constexpr size_t kCapacity = 200;                  // Sec1.3 "up to 200 (id,value) pairs"
    static constexpr float kChangeEpsilon = 0.001f;            // 0x01351020 (Sec3.3)

    // Looks the id up (inserting a fresh slot on a first sight), and calls
    // backend.setRTPCValue(id, value, kGlobalObject) only when: engineUp is
    // true AND (the id got a fresh slot, or its cached value differs from
    // `value` by more than kChangeEpsilon). Returns whether the Wwise call
    // was made. When engineUp is false the cache is not touched at all
    // (Sec1.3's common guard order: "engine down -> -0x12e" is checked
    // before the cache is consulted). The caller (e.g.
    // MixRtpcBank/SmoothedGlobalRtpc) is responsible for clearing its own
    // dirty bit on an attempt (sent or not) exactly where Sec1.3 says so.
    bool trySend(WwiseBackend& backend, uint32_t id, float value, bool engineUp);

    void clear() { slots_.fill(Slot{}); }

private:
    struct Slot {
        bool used = false;       // slot claimed by some id (findOrInsert)
        bool valueSet = false;   // that id has had a value cached at least once (our own bookkeeping)
        uint32_t id = 0;
        float value = 0.0f;
    };
    // Returns the slot index for `id` (inserting if unseen), or -1 when
    // every one of the 200 slots is occupied by a DIFFERENT id (the "0xff"
    // full-table sentinel, Sec1.3).
    int32_t findOrInsert(uint32_t id);

    std::array<Slot, kCapacity> slots_{};
};

// RTPC 0xdb2d51a3 (Sec1.3, behaviour #6). Target/current both start at 1.0
// (Sec3.1 step 3, CONFIRMED); k is Sec6 OPEN item 7.
class SmoothedGlobalRtpc {
public:
    static constexpr uint32_t kRtpcId = 0xdb2d51a3u;
    static constexpr float kThreshold = 0.05f;  // 0x012a2ee0 (Sec3.3)

    SmoothedGlobalRtpc() : k_("0x0317297c (smoothed-RTPC decay constant k)", "spec-audio-runtime.md Sec6 item 7") {}

    float target = 1.0f;
    float current = 1.0f;

    sr3luahost::OpenValue<float>& k() { return k_; }

    // One frame tick's worth of Sec1.3's rule: current := (current-target)*k
    // when current exceeds target by more than kThreshold; else snap to
    // target when different. Throws OpenStateError only on the decay branch
    // (growth/equal never touch k). The math itself always runs (pure
    // game-side state); the send through `cache` is subject to `engineUp`.
    // Value sent = current * 100.0 (the 0x012a2dd8 divisor/multiplier,
    // Sec3.3). Returns whether the Wwise call happened.
    bool tick(GlobalRtpcCache& cache, WwiseBackend& backend, bool engineUp);

private:
    sr3luahost::OpenValue<float> k_;
};

// The seven mix RTPCs (Sec1.3 0x0045f841/.../b9b4, Sec3.1 step 2, Sec3.3).
class MixRtpcBank {
public:
    static constexpr size_t kCount = 7;
    // Default ids, Sec3.3's table (0x013520ec), filled in in that order.
    static constexpr std::array<uint32_t, kCount> kDefaultIds = {
        0x10138752u, 0xe7f119bbu, 0xdb1f38ccu, 0xd0133f1bu, 0xdafafc77u, 0x293d1ed3u, 0x8d7f8461u,
    };

    MixRtpcBank() {
        ids.fill(0);
        for (size_t i = 0; i < kCount; ++i) ids[i] = kDefaultIds[i];
        for (size_t i = 0; i < kCount - 1; ++i) values[i] = 0.8f;  // Sec3.1 step 2
        values[kCount - 1] = 1.0f;
        dirty.fill(true);  // Sec3.1 step 2: "their seven dirty bytes := 1"
    }

    std::array<uint32_t, kCount> ids{};
    std::array<float, kCount> values{};
    std::array<bool, kCount> dirty{};
    bool live = false;             // mode byte 0x031728c0 (Sec3.1 step 13 sets this true)
    bool sentOnceSinceLive = false;  // "sent" byte 0x0351f908

    // One frame tick: mode 0 -> all seven sent as 0.0 (through the cache),
    // sentOnceSinceLive cleared; mode 1, first tick since going live -> all
    // seven sent once with their values, dirty cleared, sentOnceSinceLive
    // set; mode 1 afterwards -> only dirty ones. Dirty bits are cleared on
    // every attempt regardless of `engineUp` (same precedent as the
    // per-voice RTPC list, Sec1.1 0x0045d1e0: "clears each node's dirty
    // byte (even when the engine is down)"); the Wwise call itself is
    // subject to `engineUp` (via GlobalRtpcCache::trySend).
    void tick(GlobalRtpcCache& cache, WwiseBackend& backend, bool engineUp);
};

// RTPC 0x9e3669da (Sec1.3 row 0x0045f91d). sourceValue is Sec6 OPEN (no
// init value stated); shadow is this project's own "last value compared"
// bookkeeping (not a modelled engine field), so 0 is a safe starting
// sentinel for it alone.
class SingleValueRtpc {
public:
    static constexpr uint32_t kRtpcId = 0x9e3669dau;

    SingleValueRtpc() : sourceValue_("0x031728d8 (RTPC 0x9e3669da source int)", "spec-audio-runtime.md Sec6 item 7") {}

    sr3luahost::OpenValue<int32_t>& sourceValue() { return sourceValue_; }

    // Sends (float)sourceValue through `cache` only when it differs from
    // the shadow; the shadow is always updated to match, even when
    // `engineUp` is false (Sec1.3: "the shadow is updated even when the
    // engine is down").
    bool tick(GlobalRtpcCache& cache, WwiseBackend& backend, bool engineUp);

private:
    sr3luahost::OpenValue<int32_t> sourceValue_;
    int32_t shadow_ = 0;
};

// The listener (Sec1.3 0x0045f690/0x0045f733, behaviour #8). The arm
// counter is plain gate state (0 = unarmed, the neutral "never armed"
// start); the position itself is Sec6 OPEN (no stated initial value).
class ListenerState {
public:
    ListenerState() : position_("0x031728e4.. (listener position)", "spec-audio-runtime.md Sec6 item 7") {}

    sr3luahost::OpenValue<ListenerPosition>& position() { return position_; }

    int32_t armCounter = 0;  // 0x03172908

    void arm(int32_t frames) { armCounter = frames; }

    // Sends only while armCounter > 0 (then decrements it regardless of
    // `engineUp`, per Sec1.3's explicit note). Throws OpenStateError if a
    // send is actually due (armCounter > 0) and the position was never set
    // - the 9 floats are read into the call regardless of the engine-up
    // guard in the real wrapper, so the read is attempted either way.
    bool tick(WwiseBackend& backend, bool engineUp);

private:
    sr3luahost::OpenValue<ListenerPosition> position_;
};

}  // namespace sr3audioruntime
