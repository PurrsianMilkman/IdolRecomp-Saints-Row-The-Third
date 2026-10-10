#pragma once

// sr3audioruntime - Wwise-INTERNAL mechanics, past the WwiseBackend
// boundary. wwise_backend.h's own header comment says of NullWwiseBackend:
// "Nothing about Wwise's own internals is modelled" - that was true until
// this pass, which adds the three Wwise-internals units
// spec-audio-runtime.md gained (HANDOFF.md's Wwise-internals workstream):
//
//   Sec8 (unit U-W1):  HIRC load - the per-item kind-switch dispatch
//                       (0x00f588c0, a 19-entry jump table) and the shared
//                       find-or-create registry pattern every non-fallback
//                       kind's construction helper uses (Sec8.2.3).
//   Sec9 (unit U-W2a): PostEvent's real internal path - Event-id lookup
//                       (0x00f579f0, 193-bucket), the deferred post-job
//                       enqueue (0x00f52c80), and the Action-object family
//                       (AddRef/Release, the one CONFIRMED hand-off into
//                       container dispatch, 0x00f76d70 -> 0x00f52320).
//   Sec7 (unit U-W2b): container-type dispatch (0x00f52320) and voice/
//                       playback-slot instantiation, down to the pool
//                       allocator's (0x00f53c40) capacity/eviction
//                       behaviour.
//
// This is a SEPARATE, deeper layer from AudioRuntime/WwiseBackend: those
// two model the GAME side of the boundary (spec-audio-runtime.md Sec1-6);
// everything here models what Sec7/8/9 found on the WWISE side of that
// SAME boundary. Nothing here is wired into WwiseBackend's existing
// postEvent()/loadBank() bodies - those remain exactly the black-box
// game/engine crossing AudioRuntime already depends on (changing their
// signatures or behaviour would risk the already-passing Sec1-6 contract
// this library's other tests pin down). This module is the "what the real
// engine does after that crossing" model the task asked for, following
// the identical null-backend discipline one layer deeper: deterministic,
// in-process, no real engine, CONFIRMED mechanics only, OpenValue/
// OpenStateError for every gap the spec itself leaves open rather than an
// invented value.
//
// Clean-room note: every identifier below is either a plain hex address
// (in a comment) or this project's own descriptive name. No kind-name or
// verb-name from Sec8.2.1/Sec9.4.3 is asserted here - spec-audio-
// runtime.md Sec13's own clean-room check flags those as "Team-A-internal
// public/community taxonomy... never cited to Team B as fact" - so HIRC
// kinds are modelled purely as the numeric values [1,19] the real jump
// table dispatches on, and the Event/Action registries are kept separate
// from that numeric kind space (Sec9's own text never ties a specific
// Sec8.2.1 kind value to "Event" or "Action").

#include <cstdint>
#include <deque>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sr3luahost/open_state.h"

namespace sr3audioruntime::wwise_internal {

// =====================================================================
// Sec8 (U-W1): HIRC load - per-item kind dispatch
// =====================================================================

// The HIRC item's own 1-based kind byte, exactly as 0x00f588c0's jump
// table indexes it (Sec8.2.1: `ecx = kind - 1; cmp ecx, 0x12; ja fallback;
// jmp [ecx*4 + table]` - a 19-entry table, CONFIRMED by direct read of
// both the bounds check and the 19 raw jump-table dwords).
using HircKind = uint8_t;
constexpr HircKind kHircKindMin = 1;
constexpr HircKind kHircKindMax = 19;

inline bool IsValidHircKind(HircKind kind) { return kind >= kHircKindMin && kind <= kHircKindMax; }
// Sec8.2.1: kinds 10,11,12,13 are routed to the shared "unregistered"
// fallback by 4 explicit, identical jump-table entries, not a gap in the
// table - CONFIRMED structural fact (matches A7's independent content
// finding that exactly 4 HIRC kinds go unused in this build's banks).
inline bool IsUnregisteredFallbackKind(HircKind kind) { return kind >= 10 && kind <= 13; }

struct HircItemDesc {
    HircKind kind = 0;
    uint32_t id = 0;  // the item's own declared object id (Sec8.2.3: item_header[+1])
};

enum class HircDispatchResult {
    kAllocatedNew,          // Sec8.2.3: first time this (kind,id) was seen -> allocate path
    kAppliedUpdate,         // Sec8.2.3: already registered -> apply/update-fields path
    kUnregisteredFallback,  // Sec8.2.1/8.2.2: kind in [10,13], or kind outside [1,19]
};

// Sec8.2.3's shared find-or-create flow, CONFIRMED for 6 of 15 per-kind
// helpers (kinds 1,2,3,4,17,19) and HIGH (identical compiled shape, not
// individually re-verified) for the other 9. Sec8.2.3 explicitly leaves
// OPEN which of the 19 kinds share one sub-manager object versus get their
// own (the "extra arg" column groups some kinds under `[0x2cc5310]` with
// no offset, some under caller-supplied `ebx`, and four under their own
// large fixed offsets - but never ties any of that to a specific kind
// NAME). Rather than assert an unconfirmed grouping, this models the
// finest-grained partition consistent with every CONFIRMED behaviour: one
// logical find-or-create table per kind value. A caller that wants to
// model several kinds sharing one registry (per Sec8.2.3's own HIGH
// reading of the "extra arg" column) can route multiple kinds' items
// through the SAME HircRegistry instance by hand instead of one-per-kind;
// this class itself does not assert either grouping.
class HircRegistry {
public:
    // Sec8.2.2: the optional registrable extension point at the global
    // [0x02cc536c], called with (ebx, ebp, &stackbuf) in the real engine
    // when no case claims the item - modelled here as (kind, id) since
    // this project gives those three real arguments no shape. Absent by
    // default, matching "nothing was found writing a non-null value to
    // [0x02cc536c] anywhere in the scanned Wwise range" in the shipped
    // build (Sec8.2.2's own CONFIRMED negative result).
    using UnregisteredCallback = std::function<void(HircKind kind, uint32_t id)>;
    void setUnregisteredCallback(UnregisteredCallback cb) { unregisteredCallback_ = std::move(cb); }

    // Sec8.2.1's bounds check plus Sec8.2.3's find-or-create flow for an
    // in-range, non-fallback kind.
    HircDispatchResult dispatchItem(const HircItemDesc& item);

    bool contains(HircKind kind, uint32_t id) const;
    size_t registeredCount(HircKind kind) const;

private:
    UnregisteredCallback unregisteredCallback_;
    std::unordered_map<HircKind, std::unordered_set<uint32_t>> registered_;
};

// =====================================================================
// Sec9 (U-W2a): Event lookup + deferred post-job enqueue
// =====================================================================

// Sec9.2.2's generic lookup primitive (0x00f579f0) hashes by `id mod 193`
// via an MSVC reciprocal-multiply idiom (`hash = id*0x15390949 >>4 *0xc1;
// hash = id - hash`); that idiom is, by construction, a bit-exact
// computation of `id % 193` (replacing a division with that exact result
// is the entire point of the compiler idiom, not an approximation of it),
// so this project computes the CONFIRMED divisor directly rather than
// reproducing the multiply/shift sequence instruction-for-instruction.
// Sec7.5.1's CAkRegistryMgr lookup uses the SAME divisor independently.
constexpr uint32_t kRegistryBucketCount = 193;  // Sec9.2.2 / Sec7.5.1, CONFIRMED (both sites agree)
inline uint32_t HashBucket193(uint32_t id) { return id % kRegistryBucketCount; }

// Sec9.3's deferred post-job record (0x00f52c80), CONFIRMED byte layout -
// trimmed to the fields with observable meaning here; the refcount/
// next-pointer/hash-bucket-link fields are a storage detail of the real
// engine's own 31-bucket table (same precedent play_record.h's header
// comment already draws for the pool/free-list mechanics it does not
// reproduce: "the real record's own byte offset" matters, not its chain
// pointers).
struct DeferredPostJob {
    uint32_t eventId = 0;          // +0x08: the Event's own numeric id, passed straight through
    int64_t gameObjectId = 0;      // +0x0c (paramStruct field 0)
    uint32_t scratchSequence = 0;  // +0x20 (paramStruct field 1 - the 0x00101c300-returned value, Sec9.2.1 step 3)
    uint32_t uFlags = 0;           // +0x30, masked to 0xffff0004 when no callback is registered (Sec9.3)
    bool hasCallback = false;
};

// Sec9.1/9.2.1/9.3's CONFIRMED two-step PostEvent core
// (0x00f4eb90 -> 0x00f4e120): an Event-id lookup against a 193-bucket
// registry, then - on a hit only - the enqueue of a DeferredPostJob. Per
// Sec9.1: "0x00f4e120 does not itself walk the Event's action list or
// touch any container/voice code" - this class does not either.
class EventRegistry {
public:
    // The caller's own classification of which loaded HIRC item is an
    // Event (Sec9's text never ties a specific Sec8.2.1 kind-dispatch
    // numeric value to "Event" - see this file's clean-room note above -
    // so that mapping is left to the caller, e.g. a test harness, rather
    // than invented here).
    void registerEvent(uint32_t eventId) { events_.insert(eventId); }
    bool contains(uint32_t eventId) const { return events_.count(eventId) != 0; }

    // Sec9.2.1 step 2: "On a miss (eax==0), returns 0 (AK_INVALID_PLAYING_ID)
    // - no further Wwise work at all." Returns false (nothing enqueued) on
    // a miss; true (job appended) on a hit - Sec9.3's enqueue is otherwise
    // unconditional on a hit in this model (Sec9.3's OWN allocation
    // failure, "node == 0: unlock; return 2", is a separate out-of-memory
    // case this null model does not simulate, same convention as every
    // other "backend call always succeeds unless configured otherwise" in
    // wwise_backend.h).
    bool postEvent(uint32_t eventId, int64_t gameObjectId, uint32_t scratchSequence, uint32_t uFlags, bool hasCallback);

    const std::deque<DeferredPostJob>& queuedJobs() const { return jobs_; }
    size_t queuedJobCount() const { return jobs_.size(); }

    // Sec9.6: "a separate, not-fully-located drain step" is what actually
    // walks an Event's action references - reported OPEN this pass (one
    // unconfirmed lead: 0x00f5a3e0/0x00f59f90/0x00f59de0, Sec9.6), not
    // CONFIRMED. Draining is therefore not a modelled behaviour here;
    // calling this names the open item (per spec-audio-runtime.md Sec9.6)
    // instead of inventing what draining would do to the queued jobs.
    [[noreturn]] void drainQueuedJobs() {
        throw sr3luahost::OpenStateError(
            "PostEvent per-frame drain / Event action-list walk",
            "spec-audio-runtime.md Sec9.6 (unit U-W2a, the unresolved U-W2a drain seam)");
    }

private:
    std::unordered_set<uint32_t> events_;
    std::deque<DeferredPostJob> jobs_;
};

// =====================================================================
// Sec9.4 (U-W2a): the Action-object family
// =====================================================================

// Sec9.4.1/9.4.4's AddRef/Release convention ("slot 1 = AddRef, slot 2 =
// Release... a general Wwise-indexable-object convention", independently
// cross-validated against a different object family in Sec9.4.4).
class RefCounted {
public:
    void addRef() { ++refcount_; }
    // Returns true once the count reaches 0 (Sec9.4.1 slot 2: "dec
    // [this+4]; at 0, removes self from the hash table... calls the
    // dtor... frees via the shared deallocator").
    bool release() {
        if (refcount_ == 0) return true;
        return --refcount_ == 0;
    }
    uint32_t refCount() const { return refcount_; }

private:
    // Sec9.4.1 ctor 0x00f76a40: the object is live with one reference as
    // soon as it is constructed and successfully hash-inserted.
    uint32_t refcount_ = 1;
};

// One Action HIRC object (Sec9.4.1). `targetSubObject` stands in for the
// `this+0x68` field (Sec9.4.2) handed into the container-dispatch hand-off
// - what it actually references is explicitly U-W2b's own territory
// (Sec9.4.2: "what 0x00f52320 then does with the container reference at
// +0x68 is... not traced further here"), so it is an opaque, caller-set
// value rather than a modelled shape.
struct ActionObject : RefCounted {
    uint32_t id = 0;                // this+0xc (Sec9.4.1 ctor's hash-insert key)
    uint32_t targetSubObject = 0;   // this+0x68 (Sec9.4.2), opaque - 0 = unset
};

// Sec9.4.1's second confirmed registry (G1+0x320). A separate type from
// EventRegistry even though Sec9.2.2 places both inside the same G1
// aggregate, because nothing in Sec8/Sec9 ties a specific Sec8.2.1 kind
// value to either one (see this file's clean-room note).
class ActionRegistry {
public:
    // Sec9.4.1 ctor's find-or-create shape (0x00f769f0's hash-insert):
    // first time this id is seen, a fresh ActionObject is created with one
    // live reference; an already-registered id instead gets AddRef'd and
    // returned (Sec9.4.4's cross-validated AddRef/Release convention).
    ActionObject& createOrGet(uint32_t id);
    ActionObject* find(uint32_t id);
    // Sec9.4.1 slot 2's "release, and at 0 remove from the registry" shape.
    void release(uint32_t id);
    size_t count() const { return actions_.size(); }

private:
    std::unordered_map<uint32_t, ActionObject> actions_;
};

// =====================================================================
// Sec7 (U-W2b): container-type dispatch + voice instantiation
// =====================================================================

// Sec7.1 step 5's flags test, CONFIRMED exact mask/value - the split
// between the fast path (0x00f51cc0, HIGH: one specific concrete container
// kind) and the generic polymorphic "pick child" path (vtable[+0x10] on a
// DIFFERENT object at esi+4, owner UNRESOLVED - Sec7.6). Recorded for
// field-value parity by ContainerCandidate below; neither path's own
// internals are modelled (Sec7.6: an exhaustive RTTI/string scan ties no
// class name to either one).
constexpr uint32_t kContainerFastPathFlagMask = 0xffff0000u;
constexpr uint32_t kContainerFastPathFlagValue = 0x00080000u;

// A chosen container/node candidate - the "esi" of Sec7.1. This project
// does not model either concrete container kind's own child-selection
// logic, so the gate outcomes Sec7.1 attributes to vtable calls on
// UNRESOLVED owners (Sec7.6) are supplied by the caller rather than
// computed here.
struct ContainerCandidate {
    uint32_t id = 0;                  // esi+8 (Sec7.1 step 3's id argument)
    bool fastPathEligible = false;    // *(esi+0x1c) & 0xffff0000 == 0x80000 (Sec7.1 step 5, CONFIRMED test)
    bool proceedPrecondition = true;  // vtable[ebp+0x7c] result (Sec7.1 step 3, UNRESOLVED owner, Sec7.6)
    bool childChosen = true;          // edi != 0 (Sec7.1 step 6): whichever path ran found a child/leaf at all
    bool childEligible = true;        // vtable[edi+8] re-check (Sec7.1 step 7, UNRESOLVED owner), only meaningful when childChosen
    float priority = 0.0f;            // consumed by the voice pool (Sec7.3.3, entry+0x1c)
};

enum class ContainerDispatchOutcome {
    kAbandonedPreconditionFailed,  // Sec7.1 step 3 false: no child ever chosen
    kAbandonedNoEligibleChild,     // Sec7.1 step 6: edi == 0
    kAbandonedChildRecheckFailed,  // Sec7.1 step 7 re-check false: child freed, abandoned
    kInstantiated,                 // Sec7.1 step 7 success -> Sec7.3's voice-instantiation chain ran
    kInstantiationFailed,          // Sec7.3.3's pool returned no instance (capacity full, nothing to evict)
};

struct VoiceInstance {
    uint32_t candidateId = 0;
    float priority = 0.0f;
};

// Sec7.3.3's voice/playback-slot pool allocator (0x00f53c40): a hard
// capacity limit plus lowest-priority eviction when full. The exact
// tunables (the hard count limit itself, the real priority-recompute
// formula of 0x00f76580, the quantized-start-time global of 0x00f76710)
// are given no concrete value anywhere in Sec7 (HIGH-confidence ROLES
// only) - the capacity limit is caller-configured rather than invented;
// the priority-recompute/volume/dedup bookkeeping is folded down to the
// one externally observable contract Sec7.3.3 itself confirms end-to-end:
// full -> evict the lowest-priority existing instance to free a slot;
// otherwise allocate a fresh one; either way the chosen candidate's own
// `priority` (caller-supplied, see ContainerCandidate) becomes the new
// instance's stored priority (Sec7.3.3: "entry+0x1c, the exact field just
// compared during eviction - confirming the round-trip").
class VoiceInstancePool {
public:
    explicit VoiceInstancePool(size_t capacityLimit) : capacityLimit_(capacityLimit) {}

    // Returns nullptr only on Sec7.3.3's own "if one is found" guard
    // failing - at/over capacity with no existing instance to evict
    // (capacityLimit == 0). Otherwise always succeeds (this null model
    // does not simulate 0x00f76710's own internal rollback-on-failure
    // path, Sec7.3.3's "result == 2" case - no concrete trigger for that
    // result is given anywhere in Sec7).
    VoiceInstance* instantiate(uint32_t candidateId, float priority);

    size_t size() const { return instances_.size(); }
    size_t capacityLimit() const { return capacityLimit_; }
    const std::vector<VoiceInstance>& instances() const { return instances_; }

private:
    size_t capacityLimit_;
    std::vector<VoiceInstance> instances_;
};

// Sec7.1's CONFIRMED container-type dispatch root (0x00f52320), through
// Sec7.3's voice-instantiation hand-off on success.
class ContainerDispatchRoot {
public:
    explicit ContainerDispatchRoot(VoiceInstancePool& pool) : pool_(pool) {}
    ContainerDispatchOutcome dispatch(const ContainerCandidate& candidate);

private:
    VoiceInstancePool& pool_;
};

// Sec9.4.2's CONFIRMED, unconditional hand-off (0x00f76d70): applying an
// Action forwards (this, &this->targetSubObject) straight into U-W2b's
// container-dispatch root above - "resolving a concrete Action object and
// invoking this method is, operationally, 'applying' the action" (Sec9.4.2).
// `action` itself carries no further role in the call once the hand-off
// happens (its vt_ca68 slots 7/8 verb-dispatch tables, Sec9.4.3, are
// documented in this header's comments only - the spec never confirms what
// calls those tables or when, so wiring them into this function would
// invent a trigger this pass never found).
inline ContainerDispatchOutcome ApplyAction(const ActionObject& action, ContainerDispatchRoot& root,
                                             const ContainerCandidate& resolvedTarget) {
    (void)action;
    return root.dispatch(resolvedTarget);
}

// Sec7.3.1's global active-instance list (0x02cc5430/0x02cc542c/0x02cc5434)
// that every successfully-instantiated child is appended to, distinct from
// the game-side 0xa8-stride voice pool spec-audio-runtime.md Sec2.2 already
// documents - VoiceInstancePool::instances() above models exactly that
// append-on-success list. Sec7.3.4's re-evaluation entry point
// (0x00f51eb0, "not reached from 0x00f52320 in this pass's trace") is
// documented, not implemented, for the same reason the Sec9.4.3 verb
// tables are not: no confirmed caller/trigger was found for it either.

}  // namespace sr3audioruntime::wwise_internal
