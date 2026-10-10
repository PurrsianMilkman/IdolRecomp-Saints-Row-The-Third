#pragma once

// sr3physics/character_proxy.h - the character-proxy lock-step pass cap and
// MyCharacterListener's queue-only contract.
//
// Source: spec-physics-runtime.md Sec2.1 rows 21-23, Sec2.2 and Sec4.6 item
// 3 (SPEC TEAM, physics-vehicle boundary unit), cross-referenced 2026-10-03
// to spec-player-runtime.md Sec5.6.4 (the lock-step pass loop, read from
// the game side) and Sec5.8 (the listener's queue targets and merge rule).
// Every address/count below is copied from those two sections so a reader
// can cross-check; nothing here was derived from the executable, and no
// decompiled code or pseudocode appears below.
//
// WHAT IS IMPLEMENTED:
//   * CharacterProxyStepper: the "up to 10 lock-step cast-and-solve passes"
//     cap (CONFIRMED - spec-physics-runtime.md Sec4.6 item 3: "not a fixed
//     sub-step count ... capped at 10"; spec-player-runtime.md Sec5.6.4
//     step 3: "At most 10 passes per frame") - a pass loop that stops as
//     soon as a proxy's own remaining time is exhausted (> 1.19e-7, the
//     spec's own epsilon - Sec5.6.4 step 3's literal "0079ddc0(last = (p =
//     9))... whether time remains (> 1.19e-7)"), capped at 10 passes
//     regardless of remaining time. runBatch() models the real LOCK-STEP
//     shape: every proxy still active advances together each pass,
//     individually dropping out once its own time is spent, under one
//     SHARED pass cap for the whole batch (Sec5.6.4 step 3: "every listed
//     proxy advances one cast-and-solve... join again").
//   * CharacterProxyListener<ObjectRecord, CharacterRecord>: the three
//     Havok-callable slots of row 23 (processConstraintsCallback
//     0x007a2960, characterInteractionCallback 0x0079fb80,
//     objectInteractionCallback 0x007a45f0 - slot 0, 0x0079f8d0, is the
//     scalar deleting destructor, not a callback, spec-player-runtime.md
//     Sec5.8) - "ALL THREE ONLY QUEUE" (spec-physics-runtime.md Sec2.2,
//     CONFIRMED), into the two fixed-size (<=100 entries) queues
//     0x0223d3b0 (0x70 bytes/entry, fed by objectInteractionCallback) and
//     0x0223ff70 (0x50 bytes/entry, fed by characterInteractionCallback) -
//     spec-player-runtime.md Sec5.8 - merging duplicate (human, object)
//     pairs by a running average (both specs).
//
//     THE LISTENER QUEUE-ONLY CONTRACT (this unit's own name for the
//     requirement): each callback is handed ONLY an EnqueueOnly<T>&, which
//     exposes push() and nothing else - no world handle, no game-state
//     reference, no getter, no way back to anything this library owns. A
//     callback whose body takes no OTHER captures is therefore structurally
//     incapable of mutating anything but the queue it was given - the
//     dispatch mechanism (fireObjectInteraction() etc.) never gives it
//     anything else to mutate. This does not, and cannot in C++, stop a
//     callback that CLOSES OVER some outside mutable reference from misusing
//     it - that risk exists for any injected std::function in this
//     codebase (e.g. sr3player's own hooks) and is a caller-discipline
//     matter, not something this header's API surface can prevent; what IS
//     prevented structurally is the dispatcher itself ever exposing game
//     state to the callback.
//
// WHAT IS NOT IMPLEMENTED (out of scope, P5 material per spec-player-
// runtime.md Sec5.6.4's own framing):
//   * The cast-and-solve internals (0x0079b000-0x0079e000) - SolvePass is
//     an injected stand-in (Scope discipline: no real collision/solver).
//   * processConstraintsCallback's own per-contact class/swimming tests
//     (578 instructions, "not read further" - spec-player-runtime.md
//     Sec5.8) and WHICH of the two queues (if either) it feeds - not
//     established by either spec unit; modelled with access to BOTH queues
//     (see onProcessConstraints below) rather than guessed onto one.
//   * Scheduler job group 0x0223ac40 itself - no real job scheduler exists
//     in this project (Scope discipline); passes here run synchronously, in
//     whatever order the caller drives them.

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace sr3physics {

// --- the lock-step pass cap ------------------------------------------------

class CharacterProxyStepper {
public:
    // CONFIRMED cap - spec-physics-runtime.md Sec4.6 item 3: "up to 10
    // lock-step cast-and-solve passes ... capped at 10"; spec-player-
    // runtime.md Sec5.6.4 step 3: the 10th pass (p == 9, 0-indexed) is
    // always the last one, whether or not time remains.
    static constexpr int kMaxPasses = 10;
    // "whether time remains (> 1.19e-7)" - spec-player-runtime.md Sec5.6.4
    // step 3, CONFIRMED literal.
    static constexpr double kRemainingTimeEpsilon = 1.19e-7;

    // One proxy's state across a batch: its own remaining time, and how
    // many passes it has actually been given (<= kMaxPasses).
    struct ProxyState {
        double remaining = 0.0;
        int passesRun = 0;
    };

    // One pass over the time still remaining for one proxy; returns how
    // much of it this pass consumed. Internals out of scope (see the file
    // doc comment above) - a port/test supplies its own stand-in.
    using SolvePass = std::function<double(double remaining)>;

    // Single proxy (a runBatch() of one entry). Returns the number of
    // passes actually run (<= kMaxPasses).
    int run(double frameDt, const SolvePass& solve);

    // The real lock-step shape: every proxy still active (remaining time >
    // epsilon) gets one solve() call per pass; a proxy whose remaining
    // time drops to/below the epsilon drops out of later passes within the
    // SAME call (it is not "listed" again - spec-player-runtime.md Sec5.6.4
    // step 3: "every LISTED proxy"). The shared pass index is capped at
    // kMaxPasses for the whole batch, not per proxy - a proxy that still
    // has time left when the cap is hit simply stops there (CONFIRMED: the
    // cap is a loop property, not a per-proxy exhaustion test). Returns the
    // number of passes the batch actually ran.
    int runBatch(std::vector<ProxyState>& proxies, const SolvePass& solve);
};

// --- the listener queue-only contract --------------------------------------

// What a callback is handed: push() and nothing else (see the file doc
// comment above for exactly what this does and does not enforce).
template <typename T>
class EnqueueOnly {
public:
    using PushFn = std::function<void(const T&)>;
    explicit EnqueueOnly(PushFn push) : push_(std::move(push)) {}
    void push(const T& record) const { push_(record); }

private:
    PushFn push_;
};

// A fixed-size (<=100 entries, CONFIRMED - spec-player-runtime.md Sec5.8:
// "two fixed-size (<= 100 entries) game-side queues") queue of interaction
// records that merges duplicate (human, object) pairs by a running average
// (spec-physics-runtime.md Sec2.2, spec-player-runtime.md Sec5.8). T's own
// key/merge logic is supplied by the port: this boundary unit does not know
// the real record layout (spec-player-runtime.md Sec5.8's own byte tables
// are game/player-runtime material, out of scope here).
template <typename T>
class QueueOnlyRecordQueue {
public:
    static constexpr size_t kMaxEntries = 100;  // CONFIRMED, spec-player-runtime.md Sec5.8

    using KeyEq = std::function<bool(const T&, const T&)>;  // same (human, object) pair?
    using Merge = std::function<void(T&, const T&)>;        // running-average merge, in place

    QueueOnlyRecordQueue(KeyEq keyEq, Merge merge) : keyEq_(std::move(keyEq)), merge_(std::move(merge)) {}

    // Pushes a new record, or merges into an existing same-key one by
    // running average. Past the cap: CHOSEN to drop silently - the spec
    // gives the cap ("holds at most 100") and not an overflow policy;
    // dropping (rather than evicting the oldest, say) matches this
    // project's other silently-dropped mutation category's spirit (spec-
    // physics-runtime.md Sec4.3's setter wrappers) instead of inventing an
    // eviction rule the spec never states.
    void pushRecord(const T& record) {
        for (auto& e : entries_) {
            if (keyEq_(e, record)) {
                merge_(e, record);
                return;
            }
        }
        if (entries_.size() < kMaxEntries) entries_.push_back(record);
    }

    // The view handed to a listener callback: push() only, no read access
    // back into this queue.
    EnqueueOnly<T> enqueueOnlyView() {
        return EnqueueOnly<T>([this](const T& r) { pushRecord(r); });
    }

    // "Flushed back into game state afterward by ordinary game code"
    // (spec-player-runtime.md Sec5.8) - called OUTSIDE any pass, never from
    // a callback.
    template <typename Fn>
    void flush(Fn&& consume) {
        for (auto& e : entries_) consume(e);
        entries_.clear();
    }

    size_t size() const { return entries_.size(); }

private:
    KeyEq keyEq_;
    Merge merge_;
    std::vector<T> entries_;
};

// MyCharacterListener's stand-in (row 23). ObjectRecord/CharacterRecord are
// the port's own record types for the two queues (0x0223d3b0 /
// 0x0223ff70).
template <typename ObjectRecord, typename CharacterRecord>
class CharacterProxyListener {
public:
    using ObjectQueue = QueueOnlyRecordQueue<ObjectRecord>;
    using CharacterQueue = QueueOnlyRecordQueue<CharacterRecord>;

    CharacterProxyListener(typename ObjectQueue::KeyEq objectKeyEq, typename ObjectQueue::Merge objectMerge,
                           typename CharacterQueue::KeyEq characterKeyEq,
                           typename CharacterQueue::Merge characterMerge)
        : objectQueue_(std::move(objectKeyEq), std::move(objectMerge)),
          characterQueue_(std::move(characterKeyEq), std::move(characterMerge)) {}

    // objectInteractionCallback (0x007a45f0) -> 0x0223d3b0 (0x70 bytes/entry).
    std::function<void(EnqueueOnly<ObjectRecord>&)> onObjectInteraction;
    // characterInteractionCallback (0x0079fb80) -> 0x0223ff70 (0x50 bytes/entry).
    std::function<void(EnqueueOnly<CharacterRecord>&)> onCharacterInteraction;
    // processConstraintsCallback (0x007a2960) - "all three only queue" is
    // CONFIRMED (spec-physics-runtime.md Sec2.2), but WHICH queue (if
    // either) this one feeds is not established by either spec unit (its
    // own internals were "not read further" - spec-player-runtime.md
    // Sec5.8); handed BOTH enqueue-only views so it structurally can still
    // only enqueue, into either, never mutate directly.
    std::function<void(EnqueueOnly<ObjectRecord>&, EnqueueOnly<CharacterRecord>&)> onProcessConstraints;

    void fireObjectInteraction() {
        auto v = objectQueue_.enqueueOnlyView();
        if (onObjectInteraction) onObjectInteraction(v);
    }
    void fireCharacterInteraction() {
        auto v = characterQueue_.enqueueOnlyView();
        if (onCharacterInteraction) onCharacterInteraction(v);
    }
    void fireProcessConstraints() {
        auto ov = objectQueue_.enqueueOnlyView();
        auto cv = characterQueue_.enqueueOnlyView();
        if (onProcessConstraints) onProcessConstraints(ov, cv);
    }

    // "Flushed back into game state afterward by ordinary game code."
    template <typename FnO, typename FnC>
    void flush(FnO&& consumeObject, FnC&& consumeCharacter) {
        objectQueue_.flush(std::forward<FnO>(consumeObject));
        characterQueue_.flush(std::forward<FnC>(consumeCharacter));
    }

    size_t objectQueueSize() const { return objectQueue_.size(); }
    size_t characterQueueSize() const { return characterQueue_.size(); }

private:
    ObjectQueue objectQueue_;
    CharacterQueue characterQueue_;
};

}  // namespace sr3physics
