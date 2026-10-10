#pragma once

// sr3audioruntime - the real per-block I/O scheduling loop (spec-audio-
// runtime.md Sec6, unit U-W5: the "Audiolib Stream Manager" thread,
// entirely game-side at 0x0046b820 (outer sleep/dispatch) /
// 0x0046c040 (per-tick dispatch)). Resolves the scoping note's own OPEN
// item for this unit ("the actual per-block I/O scheduling loop... was not
// located") and extends types.h's existing status::kIoCompletion (= 10)
// constant - already named there as "the I/O hook's read-completion" - with
// the real structure that produces it.
//
// Models the CONFIRMED structural shape only (Sec6.1-6.5):
//   - up to kConcurrentTransferSlots requests in flight at once (the two
//     persistent 16 KiB buffer slots, Sec6.5) - anything past that queues;
//   - a FIFO queue, matching the scheduler's own "pop the next pending node
//     from the queue... start a new transfer into the now-free buffer"
//     idiom (Sec6.2 step3/step5);
//   - completion is reported exactly once per request, matching "posts
//     message code 10 through 0x00471ae0(transfer info, result)"
//     (Sec3.6/Sec6.4) - the caller is expected to route each CompletionInfo
//     into the SAME status::kIoCompletion message-queue path types.h
//     already names, via AudioRuntime::postStatusMessage() or a test's own
//     stand-in; this header does not reach into AudioRuntime itself, to
//     avoid coupling a Wwise-internal structure to game-side message-queue
//     shape decisions that belong to that other file.
//
// Explicitly NOT modelled (document-and-defer, never invented - Sec6 OPEN
// items 13/14/15 and this unit's own dispatch-brief scope):
//   - the three synchronous fast paths (Sec6.3 (a)/(b)/(c): bank-resident
//     serve, resident/cache check, in-memory-package direct copy) - this
//     library has no real bank-resident memory or package index to serve
//     from, so EVERY request here takes the genuinely-queued path
//     (Sec6.3 step2 only, never the synchronous-completion step1);
//   - the 3-entry "medium" table (Sec6.2 step2/Sec6.5) and any real
//     per-medium transfer timing - no real medium/device object exists
//     here to time against;
//   - a real transfer DURATION: a request completes exactly one tick()
//     call after the tick() that started it (this project's own documented
//     modelling choice, matching the file's own precedent of "one-tick
//     latencies" elsewhere, e.g. behaviour #10 - NOT a CONFIRMED timing
//     fact, since no real disk/device exists here to time);
//   - the pending-node's own soft-named fields (Sec6 OPEN item 13:
//     heuristics/priority byte, secondary status/error flag) - CompletionInfo
//     only carries what Sec6.4 CONFIRMS the completion path itself needs
//     (which request, success/fail).

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>

namespace sr3audioruntime {

class IoRequestScheduler {
public:
    // Sec6.5: the two persistent 16 KiB buffer slots at hook+0x08/+0x4008 -
    // the real, CONFIRMED concurrency cap on in-flight transfers.
    static constexpr size_t kConcurrentTransferSlots = 2;

    struct CompletionInfo {
        uint32_t pendingId = 0;
        uint32_t transferCookie = 0;  // caller-chosen, echoed back unchanged
        bool success = true;
    };
    using CompletionSink = std::function<void(const CompletionInfo&)>;

    // Sec6.3 step2 (the queued path only - see this header's own scope
    // note): enqueues a pending request and returns a scheduler-assigned
    // id. Never completes synchronously (no fast path is modelled here).
    uint32_t requestRead(uint32_t transferCookie) {
        uint32_t id = nextId_++;
        pending_.push_back(Node{id, transferCookie});
        return id;
    }

    // Sec6.2's per-tick dispatch shape, in the order the real function
    // reads: anything already in flight from an EARLIER tick() call
    // completes first (one-tick latency, this project's own choice, see
    // header note), then free slots are refilled from the pending queue in
    // FIFO order (Sec6.2 step3/step5's free-list/queue idiom).
    void tick(const CompletionSink& onComplete) {
        for (const Node& n : inFlight_) {
            if (onComplete) onComplete(CompletionInfo{n.id, n.transferCookie, true});
        }
        inFlight_.clear();
        while (inFlight_.size() < kConcurrentTransferSlots && !pending_.empty()) {
            inFlight_.push_back(pending_.front());
            pending_.pop_front();
        }
    }

    size_t pendingCount() const { return pending_.size(); }
    size_t inFlightCount() const { return inFlight_.size(); }

private:
    struct Node {
        uint32_t id;
        uint32_t transferCookie;
    };
    std::deque<Node> pending_;
    std::deque<Node> inFlight_;
    uint32_t nextId_ = 1;
};

}  // namespace sr3audioruntime
