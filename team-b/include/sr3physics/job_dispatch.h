#pragma once

// sr3physics/job_dispatch.h - the Havok job queue's own per-job-type
// dispatch table: the real mechanism standing behind
// IPhysicsBackend::drainJobs() (frame_contract.h), confirmed this pass by
// direct disassembly.
//
// Source: spec-physics-runtime.md Sec9 ("World-step job dispatch, unit
// W-U0") and Sec10 OPEN items 32/33/36/38, SPEC TEAM (cleanroom
// reverse-engineering process: capstone linear-sweep disassembly against
// the raw exe, Ghidra-free, no leaked Havok/Volition/SR4 source or
// DeepWiki page read) - the ONLY source for every behaviour below.
// Addresses and counts in comments are copied from that text so a reader
// can cross-check the section; nothing here was derived from the
// executable directly by this unit, and no decompiled code or pseudocode
// appears below.
//
// RELATIONSHIP TO frame_contract.h's TWO EXISTING INTERFACES - read this
// before extending either file further, since the spec names two
// DIFFERENT queues with the word "job":
//   * IGameJobScheduler (frame_contract.h) models the OUTER queue: "the
//     game's own scheduler (class-3 queue)" that Sec4.4 says runs exactly
//     kStepJobCount (2) "game jobs" per step. W-U0 (this file) says
//     NOTHING about that queue's own internals - it is a separate
//     mechanism (Sec10 OPEN item 33's own candidate producer-side
//     functions, e.g. 0x00be8bf0, are NOT the class-3 queue). Do not fold
//     this file's job-TYPE count (4, Sec9.1) into kStepJobCount (2,
//     Sec4.4) - they are unrelated numbers from two different layers that
//     happen to share the English word "job" in the spec's own prose.
//   * IPhysicsBackend::drainJobs() (frame_contract.h) is the thing this
//     file actually opens: its existing doc comment already tagged
//     0x00be88c0 as HIGH "hkJobQueue::processAllJobs" - Sec9.0's own
//     headline finding CONFIRMS that address and that name's underlying
//     structural behaviour (the queue object itself has no vtable, so the
//     Havok public-API names are still HYPOTHESIS as NAMES - Sec9, "Method"
//     paragraph - even though the dispatch loop's own BEHAVIOUR is
//     CONFIRMED). JobTypeTable/JobQueueFifo below are that confirmed body;
//     JobQueuePhysicsBackend is a concrete IPhysicsBackend that runs it for
//     real, so a port can use it in place of NullPhysicsBackend without
//     touching frame_contract.h or PhysicsFrameDriver.
//
// WHAT IS IMPLEMENTED:
//   * JobDescriptor: the two bytes the generic dispatch loop reads off a
//     job (Sec9.0, CONFIRMED literal pseudocode: "type = jobDesc.byte[1]",
//     "subtype = jobDesc.byte[0]").
//   * JobTypeRow: the flat per-job-type row's own storage contract
//     (Sec9.0, CONFIRMED: a 16-byte row at queue+0x128+type*16 holding
//     subtypeTable/subtypeTableLength/procFunc/finishFunc) exactly as
//     registerJobType (0x00be7c90, Sec9.1, read in full) writes it: row[0]
//     = table, row[4] = length, row[8] = procFunc, row[0xc] = finishFunc.
//     An unregistered row defaults exactly as the constructor's own
//     bootstrap loop does (0x00be8960, Sec9.2, read in full): NULL table,
//     zero length, and the two named 2-instruction stub bodies as
//     procFunc/finishFunc (modelled here as a boolean "still the default
//     stub" rather than literal callables - see "stored, never invoked"
//     below for why).
//   * JobTypeTable: the row array (Sec9.0) plus registerJobType's write-once
//     discipline (Sec9.1, CONFIRMED by the single-caller result there: "the
//     whole job-type table is built exactly once, at physics bring-up ...
//     and never touched again anywhere else in the binary") - seal()
//     enforces that once a port's own bring-up is done registering.
//   * The generic dispatch loop (Sec9.0's own pseudocode, CONFIRMED,
//     processAllJobs 0x00be88c0): pull one descriptor at a time until none
//     remain, route each through table[subtype] - "not re-checked at
//     dispatch time" against the registered length (Sec9.0's own field
//     table) - modelled by giving every row a full 256-slot (one per
//     possible subtype byte, since subtype is one byte) handler array
//     where only the registered prefix is ever filled, so indexing never
//     consults subtypeTableLength either (CHOSEN: preserves the "no bounds
//     re-check" behaviour exactly without the real build's literal
//     out-of-bounds/NULL read, which is not a value to invent - see
//     JobDispatchFault's own doc comment for the unregistered-slot case).
//   * kShippedJobTypeCount / kType01SubtypeCount / kType2SubtypeCount /
//     kType3SubtypeCount: the four CONFIRMED subtype-table lengths (Sec9.3:
//     types 0 and 1 both 23, type 2 is 7, type 3 is 8) - bare confirmed
//     counts, named the same way frame_contract.h's own kStepJobCount is
//     (Sec4.4), NOT a claim that this file implements any of those
//     subtypes' real bodies (see below).
//   * JobQueueFifo: the pending-descriptor source the dispatch loop drains
//     (Sec9.0's "getJob ... if no job left: break"), as a plain injectable
//     FIFO a caller (or a test) pushes JobDescriptors into directly.
//   * JobQueuePhysicsBackend: an IPhysicsBackend whose drainJobs() really
//     runs the loop above against its own JobTypeTable + JobQueueFifo,
//     instead of NullPhysicsBackend's bare no-op - still the trivial,
//     single-threaded stand-in this project's scope is limited to (no real
//     Havok, no real multithreaded scheduling), but now also reproducing
//     the per-job-type ROUTING Sec9 confirms, not only the
//     begin/drain/finish/wait call shape frame_contract.h already had.
//     beginStep()/finishStep() stay no-ops (0x00c17920/0x00c16fc0, real
//     Havok Mt-step internals - not this unit's own material, Sec4.4).
//
// WHAT IS NOT IMPLEMENTED (OPEN / out of scope):
//   * The real subtype handler bodies for types 0-3 (Sec9.3's own table of
//     29 function addresses: the type-0/1 inner 19-way dispatcher and its
//     19 case bodies at 0x00c21d40, the 7 type-2 and 8 type-3 collision
//     handlers in Havok ranges B/C) - real Havok collision/solver internals,
//     out of scope by this project's standing scope discipline ("no real
//     collision/solver code", same as frame_contract.h's own IPhysicsBackend
//     note) regardless of W-U0's own per-function confidence level. A port
//     supplies its own Handler callables per subtype; none are provided
//     here beyond a test's own synthetic stand-ins.
//   * procFunc/finishFunc's own role or call site (Sec10 OPEN items 32/38:
//     registered by all four job types, but a whole-.text sweep for any
//     instruction reading row offset +0x8/+0xc found zero call sites
//     anywhere in the binary - "either reached only via a two-step
//     register-indirect sequence this pass's literal-displacement sweep
//     structurally cannot see, or genuinely registered-but-unused"). Stored
//     (registerJobType's contract covers storage, not invocation) but
//     dispatch() here never calls either one - matching the "no confirmed
//     caller" finding exactly rather than inventing one.
//   * Who enqueues a type-2/3 job - the producer side (Sec10 OPEN item 33:
//     "not traced ... candidates: the ring-buffer insert helper 0x00be8bf0
//     and several unopened 0x00be7xxx/0x00be8xxx siblings ... production
//     and consumption may be more tightly interleaved than a classic
//     producer/consumer split"). JobQueueFifo is this port's own stand-in
//     for "wherever getJob's descriptors come from" - this file does not
//     decide what a real physics step would push, when, or how many.
//   * The job-type count N backing the constructor's own default-fill loop
//     (Sec10 OPEN item 36: cinfo+0x1c, not pinned to a value this pass) -
//     exposed as JobTypeTable::bootstrapRowCount, a genuine OpenValue
//     rather than silently dropped or guessed; dispatch() itself does not
//     need N (see JobTypeTable's own doc comment on using the full byte
//     range instead of a committed count).
//   * The self-chaining structural detail (Sec9.3's own closing note: once
//     inside a type-2/3 handler, most opened bodies call getJob again
//     themselves and loop, rather than returning to processAllJobs) - a
//     property of the real handler BODIES this file does not implement,
//     not of the dispatch loop itself; not modelled, since every Handler
//     here is a port's own injected stand-in, free to call
//     JobTypeTable::dispatch() (or push back into its own JobQueueFifo)
//     itself if it wants to reproduce the pattern.
//   * Types 0/1's own inner 19-way dispatch, the 15 type-2/3 subtype bodies'
//     full internals, and everything else Sec9.3 only partially opened
//     (Sec10 OPEN items 34/35) - real Havok-internal content, same scope
//     note as the handler-bodies item above.
//   * Real Havok, real multithreaded job scheduling (Scope discipline, same
//     as frame_contract.h).

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "sr3luahost/open_state.h"
#include "sr3physics/frame_contract.h"

namespace sr3physics {

// --- the job descriptor (Sec9.0, CONFIRMED) --------------------------------

// "type = jobDesc.byte[1]; subtype = jobDesc.byte[0]" (Sec9.0, literal).
// The real descriptor almost certainly carries further fields the handler
// itself reads (e.g. type-2 subtype 2's 32-byte field block, Sec9.3) - out
// of scope here, since this file only models the DISPATCH layer, not any
// handler's own payload (see the file doc comment).
struct JobDescriptor {
    uint8_t subtype = 0;
    uint8_t type = 0;
};

// --- confirmed subtype-table shapes (Sec9.3) -------------------------------

// Bare confirmed counts, named the same way frame_contract.h's own
// kStepJobCount is (Sec4.4) - NOT the same "job count" as kStepJobCount:
// that one is the OUTER game-scheduler's job count (2, per physics step);
// these four are the INNER Havok job queue's own registered TYPE count and
// each type's subtype-table length (see the file doc comment's own
// "RELATIONSHIP" section).
inline constexpr int kShippedJobTypeCount = 4;   // types 0-3, Sec9.1 (exactly 4 registerJobType calls found)
inline constexpr int kType01SubtypeCount = 23;   // types 0 and 1 share one 23-slot table, Sec9.3
inline constexpr int kType2SubtypeCount = 7;     // Sec9.3
inline constexpr int kType3SubtypeCount = 8;     // Sec9.3
inline constexpr int kSubtypeByteRange = 256;    // subtype is one byte (Sec9.0) - every row's own slot count here

// --- the "no soft-fail path" fault (Sec9.2, CONFIRMED) ---------------------

// "There is no soft-fail path for an unregistered type at the subtype-
// dispatch layer" (Sec9.2) - dispatching a type/subtype pair whose row slot
// is still the bootstrap default (NULL subtypeTable) reads through a NULL
// pointer and crashes the real build. A C++ port cannot, and should not,
// reproduce a literal out-of-bounds/NULL read; this throws instead,
// deterministically, so the fault is visible rather than silently doing
// nothing. Deliberately NOT sr3luahost::OpenStateError: that type's own
// wording ("no spec gives its value, so it is not modelled") fits a VALUE
// the spec never pins down, but this case is the opposite - Sec9.2 pins
// down exactly what happens (an unrecoverable crash) and gives no softer
// alternative; reusing OpenStateError's message here would misrepresent a
// CONFIRMED fact as an open one. The job-type COUNT itself (see
// JobTypeTable::bootstrapRowCount below) is the genuinely open value in
// this unit, and that one does use OpenValue.
class JobDispatchFault : public std::runtime_error {
public:
    JobDispatchFault(uint8_t type, uint8_t subtype)
        : std::runtime_error("sr3physics job type " + std::to_string(static_cast<int>(type)) +
                              " subtype " + std::to_string(static_cast<int>(subtype)) +
                              " has no registered handler (spec-physics-runtime.md Sec9.2:"
                              " no soft-fail path for an unregistered type at the"
                              " subtype-dispatch layer)"),
          type_(type),
          subtype_(subtype) {}
    uint8_t type() const { return type_; }
    uint8_t subtype() const { return subtype_; }

private:
    uint8_t type_;
    uint8_t subtype_;
};

// --- one 16-byte "job-type row" (Sec9.0/9.1, CONFIRMED storage shape) ------

class JobTypeRow {
public:
    using Handler = std::function<void(const JobDescriptor&)>;

    JobTypeRow() { subtypeTable_.fill(nullptr); }

    // registerJobType's own per-row storage contract (0x00be7c90, Sec9.1,
    // CONFIRMED, read in full): row[0] = table, row[4] = length, row[8] =
    // procFunc, row[0xc] = finishFunc. `subtypeTable.size()` becomes the
    // CONFIRMED length; every slot beyond it is left default (empty)
    // exactly as the bootstrap loop leaves every never-registered slot
    // (Sec9.2) - dispatch does not distinguish the two cases, matching "not
    // re-checked at dispatch time" (Sec9.0). procFunc/finishFunc are
    // tracked only as "still the default stub or not" (hasCustomProcFunc_/
    // hasCustomFinishFunc_): both are confirmed STORED per type (distinct
    // per type 0/1, Sec9.3's own table) but never confirmed CALLED from
    // anywhere (Sec10 OPEN 32/38), so giving them real callable bodies here
    // would invent a caller this pass never found.
    void registerSubtypes(std::vector<Handler> subtypeTable, bool hasCustomProcFunc = true,
                           bool hasCustomFinishFunc = true) {
        subtypeTable_.fill(nullptr);
        const size_t n = subtypeTable.size() < subtypeTable_.size() ? subtypeTable.size() : subtypeTable_.size();
        for (size_t i = 0; i < n; ++i) subtypeTable_[i] = std::move(subtypeTable[i]);
        length_ = subtypeTable.size();
        registered_ = true;
        procFuncIsDefault_ = !hasCustomProcFunc;
        finishFuncIsDefault_ = !hasCustomFinishFunc;
    }

    bool isRegistered() const { return registered_; }
    // "Used only as a sanity bound in setup, not re-checked at dispatch
    // time" (Sec9.0's own field table) - exposed for a test/port to inspect
    // registration state; handlerFor() below deliberately never consults it.
    size_t subtypeTableLength() const { return length_; }
    bool procFuncIsDefault() const { return procFuncIsDefault_; }
    bool finishFuncIsDefault() const { return finishFuncIsDefault_; }

    // "table[subtype]" (Sec9.0) - no bound check against length_, by design
    // (see this class's own doc comment and the file doc comment's "no
    // bounds re-check" note).
    const Handler& handlerFor(uint8_t subtype) const { return subtypeTable_[subtype]; }

private:
    std::array<Handler, kSubtypeByteRange> subtypeTable_{};
    size_t length_ = 0;
    bool registered_ = false;
    bool procFuncIsDefault_ = true;
    bool finishFuncIsDefault_ = true;
};

// --- the queue's own per-type row table (Sec9.0) ---------------------------

// A flat array at queue+0x128 in the real build (Sec9.0); keyed here by the
// full uint8_t range (0-255) rather than a committed job-type count N
// (Sec10 OPEN item 36, the cinfo+0x1c constructor argument this pass did
// not pin down) - CHOSEN: any type value that is not one of the
// kShippedJobTypeCount (4) ever registered behaves identically whether it
// is "within N but unregistered" or "beyond N entirely" - both read the
// same bootstrap-default row (Sec9.2) - so N has no effect on this file's
// own observable dispatch contract and is not guessed at structurally; it
// is instead exposed below as bootstrapRowCount, a genuine OpenValue, for a
// caller that needs the real bound for some OTHER reason this file does
// not have.
class JobTypeTable {
public:
    // See the class doc comment above: the real constructor's own
    // job-type-count argument (Sec9.2, OPEN item 36). Not consulted by
    // registerJobType()/dispatch() below.
    sr3luahost::OpenValue<uint32_t> bootstrapRowCount{
        "hkJobQueue constructor cinfo+0x1c (job-type count N)",
        "spec-physics-runtime.md Sec9.2 OPEN item 36"};

    // registerJobType (0x00be7c90, Sec9.1, CONFIRMED storage contract).
    // Throws std::logic_error after seal() - see seal()'s own doc comment.
    void registerJobType(uint8_t type, std::vector<JobTypeRow::Handler> subtypeTable,
                          bool hasCustomProcFunc = true, bool hasCustomFinishFunc = true);

    bool isRegistered(uint8_t type) const {
        auto it = rows_.find(type);
        return it != rows_.end() && it->second.isRegistered();
    }

    // Returns the registered row, or a shared bootstrap-default row (Sec9.2)
    // for any type never passed to registerJobType().
    const JobTypeRow& rowFor(uint8_t type) const;

    // "The whole job-type table is built exactly once, at physics bring-up,
    // immediately after the queue object is constructed, and never touched
    // again anywhere else in the binary" (Sec9.1, CONFIRMED by the
    // single-caller result there). A port's own bring-up calls
    // registerJobType() for each real job type, then seal(); this is a
    // CHOSEN enforcement of that CONFIRMED one-shot fact (not itself a
    // value the spec names), matching this library's own PhysicsLifecycle
    // guard-byte precedent (lifecycle.h) of making a CONFIRMED "only once"
    // contract a loud failure rather than a silent no-op if violated.
    void seal() { sealed_ = true; }
    bool isSealed() const { return sealed_; }

    // The generic dispatch loop's own per-job routing step (Sec9.0,
    // CONFIRMED): row = table+type*16; handler = row.subtypeTable[subtype];
    // handler(desc). Throws JobDispatchFault (see that class's own doc
    // comment) when the slot is empty - covering BOTH a wholly unregistered
    // type and a subtype beyond a registered type's own length, exactly
    // matching "not re-checked at dispatch time" (Sec9.0).
    void dispatch(const JobDescriptor& desc) const;

private:
    std::unordered_map<uint8_t, JobTypeRow> rows_;
    bool sealed_ = false;
};

// --- the generic dispatch loop's own job source (Sec9.0) -------------------

// "getJob(queue, 0, &jobDesc); if no job left: break" (Sec9.0's own
// pseudocode, CONFIRMED). The real getJob (0x00be88a0/0x00be8650) reads
// from the queue's own internal ring buffer/bookkeeping block (the
// constructor's separate 0x250-byte allocation at queue+0x18, Sec9.2) - out
// of scope here (Scope discipline: no real Havok). JobQueueFifo is this
// port's own stand-in for "wherever getJob's descriptors come from": an
// injectable FIFO a caller (or a test) pushes JobDescriptors into directly
// - see the file doc comment on OPEN item 33, the real producer side.
class JobQueueFifo {
public:
    void push(JobDescriptor desc) { pending_.push_back(desc); }
    size_t pendingCount() const { return pending_.size(); }

    // Drains every pending descriptor through `table`'s dispatch(), in
    // FIFO order, stopping once empty - "if no job left: break" (Sec9.0).
    // Returns the number of jobs dispatched.
    size_t drainAll(const JobTypeTable& table);

private:
    std::deque<JobDescriptor> pending_;
};

// --- a concrete IPhysicsBackend whose drainJobs() is real (Sec9) ----------

// frame_contract.h's own IPhysicsBackend::drainJobs() doc comment already
// tagged 0x00be88c0 as HIGH hkJobQueue::processAllJobs; this class is that
// HIGH tag's CONFIRMED body, wired behind the SAME interface so a port can
// swap it in for NullPhysicsBackend without changing frame_contract.h or
// PhysicsFrameDriver. beginStep()/finishStep() stay no-ops (0x00c17920/
// 0x00c16fc0 - real Havok Mt-step internals, not this unit's own material);
// only drainJobs() does real (confirmed-shape) work: drain this backend's
// own JobQueueFifo through its own JobTypeTable.
class JobQueuePhysicsBackend : public IPhysicsBackend {
public:
    JobTypeTable jobTypes;
    JobQueueFifo pendingJobs;

    void beginStep(double) override {}
    void drainJobs() override { pendingJobs.drainAll(jobTypes); }
    void finishStep() override {}
};

}  // namespace sr3physics
