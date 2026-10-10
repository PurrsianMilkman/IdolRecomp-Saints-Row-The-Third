#pragma once

// sr3agora/agora_boundary.h - AgoraBoundary: the GAME side of the Agora
// Games "Hydra SDK" / GDS telemetry call-boundary contract
// (spec-agora-hydra-boundary.md), built against the AgoraBackend interface
// (agora_backend.h). See that header's own doc comment for the backend/
// null-backend split this follows.
//
// Implements:
//   Sec2.1/Sec2.2 - the init wrapper's exact contract, INCLUDING its one
//     headline divergence from sr3steam's own SteamAPI_Init contract: the
//     "active" flag is set BEFORE the real init call even runs and is
//     NEVER rolled back on failure (Sec2.1 step3's own words), so unlike
//     SteamBoundary::init() (which allows a retry after a failed call,
//     since Steam's guard literally stores Init's own last return value),
//     AgoraBoundary::init() only ever genuinely runs ONCE per instance -
//     every later call, success or failure, is a no-op that replays the
//     FIRST call's own cached result (Sec2.2's own "Contract summary":
//     "since this function is called exactly once per process... if it
//     fails for any reason on a given run, the readiness flag never
//     becomes 1 for the rest of that process's life").
//   Sec3.1/Sec3.2 - shutdown: resets the session-id sentinel pair to
//     (-1,-1) and the connected flag to false (the exact values a fresh,
//     never-connected process already starts at), clears the per-key
//     dedup registry, and sets the readiness state to ShutDown - but,
//     CONFIRMED by the disassembly (Sec3.1's own listing has no instruction
//     touching it), does NOT reset the "active" flag, so a later init()
//     call remains the permanent no-op described above.
//   Sec4.1 - the shared queue wrapper's CONFIRMED gating shape (readiness
//     check, field-count check), modelled generically as submitReport()
//     rather than reproducing the ~65 individually-coded report functions
//     (their own embedded 32-bit "which milestone" constants are opaque,
//     unresolved - OPEN item 10 - exactly as unnamed here as the spec
//     leaves them, never invented).
//   Sec4.2/Sec4.2.1 - the two structural shapes: Shape A (reportMilestone)
//     is gated by the dual existence/connectivity check BEFORE it ever
//     reaches the queue wrapper; Shape B (reportEvent) is NOT - it "takes a
//     this-style pointer... with no existence check beforehand" and goes
//     straight to the queue wrapper, which still applies its OWN readiness
//     check (Sec4.1 step2) regardless of which shape called it.
//   Sec5.1 - the HTTP response status-code dispatch: 200 decodes (stubbed -
//     no real body to decode), 400-499 permanently kills the readiness
//     flag, everything else resets-and-continues with no readiness change.
//
// NOT modelled (OPEN, carried forward rather than invented - see
// agora_backend.h's own banner for the matching list of calls this
// leaves off AgoraBackend):
//   - The master config flag gate, the four config-override arguments, the
//     post-init hook, the three debug commands, shutdown's untraced helper
//     calls beyond the CONFIRMED sentinel reset.
//   - WHO ever sets the session-id pair away from (-1,-1) or the connected
//     flag away from false (Sec4.2 Shape B, OPEN item 12) - setSessionId()/
//     setConnected() below are test-settable SEAMS, exactly mirroring
//     sr3steam's "every behaviour individually forceable" null-backend
//     convention, not a claim about what triggers them in the real game.
//   - The exact state-machine transitions a connection-level failure (no
//     HTTP response at all) takes (Sec5.1's own closing paragraph, OPEN
//     item 15) - per Sec5.2 point 4's own explicit guidance, a null
//     backend does not need to simulate this at all: by default, nothing
//     calls handleHttpResponse() here, so a submitted report simply never
//     resolves, exactly as "a dead server" behaves.

#include <cstdint>
#include <unordered_set>
#include <utility>

#include "sr3agora/agora_backend.h"
#include "sr3agora/agora_value.h"

namespace sr3agora {

// Sec2.2 step2/step6 and Sec3.2's own literal values: the master readiness
// flag 0x031719bc only ever holds one of these three values.
enum class AgoraReadiness : int32_t {
    NotReady = -1,  // never succeeded, or killed by a 4xx response (Sec5.1)
    ShutDown = 0,   // Sec3.2's own post-shutdown value
    Ready = 1,      // Sec2.2 step6's success value
};

// Sec5.1's three-way HTTP response outcome.
enum class AgoraHttpOutcome {
    Ok,             // status 200: decode step (stubbed - no real body here)
    ClientError4xx, // status 400-499: readiness permanently killed
    Other,          // 5xx / anything else: reset-and-continue, no change
};

// Sec4.1's queue-wrapper outcome, returned for test inspection - the real
// game's own ~65 call sites ignore this entirely (Sec4.1's own closing
// point: "every sampled call site... ignores the return value entirely").
struct AgoraSubmitOutcome {
    bool queued = false;       // true only on Sec4.1 step6's success path
    uint64_t sequenceNumber = 0;  // valid only when queued == true
};

// AgoraBoundary - the GAME side of the contract. One instance would
// normally correspond to the single, unconditional, once-per-process
// init()/shutdown() pair Sec2.1/Sec3.1 each describe exactly one call site
// for.
class AgoraBoundary {
public:
    explicit AgoraBoundary(AgoraBackend& backend) : backend_(backend) {}

    // Sec2.1 steps 2-9 + Sec2.2 steps 1-6, in order. Returns the same bool
    // the real init function itself returns (true only on Sec2.2 step6).
    //
    // - First call: derives client identity (Sec2.2 step3), then the two
    //   untraced failure helpers (step4/step5) in order; either failing
    //   returns false with readiness left NotReady; both passing sets
    //   readiness Ready and returns true.
    // - Every call after the first: a no-op that replays the FIRST call's
    //   own result without touching the backend again at all - see the
    //   file banner's own contrast with SteamBoundary::init(). This is
    //   intentional and CONFIRMED (Sec2.1 step3: "never roll it back on
    //   failure"; Sec2.2's own contract summary: the real init function is
    //   "called exactly once per process").
    bool init();

    bool isActive() const { return active_; }
    AgoraReadiness readinessState() const { return readiness_; }
    bool isReady() const { return readiness_ == AgoraReadiness::Ready; }

    // Sec3.1/Sec3.2. A no-op if init() was never called (Sec3.1: "if
    // active flag == 0: return immediately") or if already shut down
    // (Sec3.2 step1: "if readiness == 0: skip to ret"). On a genuine
    // shutdown: resets the session-id sentinel pair and the connected
    // flag to their fresh-process defaults, clears the per-key dedup
    // registry (Sec3.2's own "walks and frees a small hash table"), and
    // sets readiness to ShutDown. Does NOT clear the active flag (see
    // class banner).
    void shutdown();

    // Sec4.2.1, 0x013521c0/0x013521c4: the session-id sentinel pair.
    // Starts at (-1,-1) - "the exact pair shutdown's own reset function
    // writes," i.e. the fresh/no-session value - and nothing CONFIRMED in
    // this spec's own scope ever sets it to anything else (see file
    // banner); exposed as a settable test seam only.
    void setSessionId(int32_t a, int32_t b) { sessionId_ = {a, b}; }
    std::pair<int32_t, int32_t> sessionId() const { return sessionId_; }
    bool hasSession() const { return !(sessionId_.first == -1 && sessionId_.second == -1); }

    // Sec4.2 Shape B's one write site, 0x031719f4: the "connected" flag -
    // CONFIRMED by PE-format guarantee to start at 0 (false) on every
    // process launch (zero-filled .data extension, loader-zeroed, no
    // static initializer). Settable test seam only (see file banner).
    void setConnected(bool connected) { connected_ = connected; }
    bool isConnected() const { return connected_; }

    size_t reportedMilestoneCount() const { return reportedKeys_.size(); }

    // Sec4.2 Shape A (63 functions): gated by the dual existence/
    // connectivity check (Sec4.2.1, reportGateOpen) BEFORE anything is
    // built or submitted. The gate's own two failure conditions (no
    // session; not connected) and its dedup check ("already reported")
    // all collapse into the identical early-out the real gate's own
    // `test eax,eax; je continue` makes no distinction between - so this
    // returns a single bool outcome exactly as faithfully.
    AgoraSubmitOutcome reportMilestone(uint32_t milestoneKey, const AgoraValueArray& fields);

    // Sec4.2 Shape B (2 reusable functions): "no existence check
    // beforehand" - skips reportGateOpen entirely and goes straight to the
    // queue wrapper, which still applies its OWN readiness check
    // regardless (Sec4.1 step2).
    AgoraSubmitOutcome reportEvent(const AgoraValueArray& fields);

    // Sec5.1: the HTTP response status-code dispatch. Nothing in this
    // library calls this on its own (see file banner) - it exists so the
    // CONFIRMED dispatch logic itself is exercisable.
    AgoraHttpOutcome handleHttpResponse(int32_t statusCode);

private:
    // Sec4.2.1: true only when a report should actually be built and
    // submitted (the gate's own eax==0 "proceed" path) - marks the key as
    // reported as a side effect of opening, matching "inserts the entry
    // and returns 0" in the same step the check itself happens.
    bool reportGateOpen(uint32_t milestoneKey);

    // Sec4.1: the shared queue/encode wrapper's CONFIRMED gating shape.
    AgoraSubmitOutcome submitReport(const AgoraValueArray& fields);

    AgoraBackend& backend_;

    bool active_ = false;       // 0x029897a5, Sec2.1 step2/3 - sticky forever once set
    bool lastInitSucceeded_ = false;
    AgoraReadiness readiness_ = AgoraReadiness::NotReady;  // 0x031719bc

    std::pair<int32_t, int32_t> sessionId_{-1, -1};  // 0x013521c0/0x013521c4
    bool connected_ = false;                         // 0x031719f4
    std::unordered_set<uint32_t> reportedKeys_;       // the dedup registry, Sec4.2.1
};

}  // namespace sr3agora
