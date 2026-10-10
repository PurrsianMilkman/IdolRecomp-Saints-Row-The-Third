// sr3agora/agora_boundary.h implementation. See the header's own doc
// comment for the exact spec-agora-hydra-boundary.md citations.
#include "sr3agora/agora_boundary.h"

namespace sr3agora {

bool AgoraBoundary::init() {
    // Sec2.1 step2/step3: the "active" flag is THE idempotency guard, and
    // it is set BEFORE the real init call even runs - so every call after
    // the first is a no-op that replays the first call's own result,
    // regardless of whether that first call succeeded or failed (step3:
    // "never roll it back on failure"). This is the one headline
    // divergence from sr3steam's own SteamAPI_Init guard (see class
    // banner).
    if (active_) return lastInitSucceeded_;
    active_ = true;

    // Sec2.2 step2: the readiness flag is set to NotReady unconditionally
    // at the start of the real init function, before anything else runs.
    readiness_ = AgoraReadiness::NotReady;

    // Sec2.2 step3: derive the client/session identity. Nothing CONFIRMED
    // past this point ever reads the result back out within this
    // contract's own scope (see agora_backend.h's own banner) - the call
    // happens purely for call-order fidelity.
    (void)backend_.deriveClientIdentity();

    // Sec2.2 step4/step5: the two untraced failure helpers, in order -
    // either one returning true for "fail" aborts immediately, leaving
    // readiness at NotReady.
    if (backend_.initFailureHelperA()) {
        lastInitSucceeded_ = false;
        return false;
    }
    if (backend_.initFailureHelperB()) {
        lastInitSucceeded_ = false;
        return false;
    }

    // Sec2.2 step6: success.
    readiness_ = AgoraReadiness::Ready;
    lastInitSucceeded_ = true;
    return true;
}

void AgoraBoundary::shutdown() {
    // Sec3.1: "if active flag == 0: return immediately (never initialized
    // / already shut down)."
    if (!active_) return;

    // Sec3.2 step1: "if readiness == 0: skip to ret (already shut down)."
    if (readiness_ == AgoraReadiness::ShutDown) return;

    // Sec3.2's own 0x00402fe0 call: resets the session-id sentinel pair to
    // (-1,-1), the connected flag to 0, and "walks and frees a small hash
    // table" - the per-key dedup registry (Sec4.2.1).
    sessionId_ = {-1, -1};
    connected_ = false;
    reportedKeys_.clear();

    // Sec3.2: readiness -> ShutDown (distinct from the NotReady value both
    // Sec2.2 and Sec5.1 use). CONFIRMED: the "active" flag itself is NOT
    // touched anywhere in Sec3.1's own instruction listing, so it stays
    // set - a later init() call remains the permanent no-op described in
    // the class banner.
    readiness_ = AgoraReadiness::ShutDown;
}

bool AgoraBoundary::reportGateOpen(uint32_t milestoneKey) {
    // Sec4.2.1: "both halves of the session-id pair are the -1 sentinel" ->
    // no-op.
    if (sessionId_.first == -1 && sessionId_.second == -1) return false;
    // Sec4.2.1: the connected flag is zero -> no-op.
    if (!connected_) return false;
    // Sec4.2.1: "walks the hash table... returns a nonzero 'found' result,
    // or inserts the entry and returns 0 ('not found, now recorded')."
    if (reportedKeys_.count(milestoneKey) != 0) return false;
    reportedKeys_.insert(milestoneKey);
    return true;
}

AgoraSubmitOutcome AgoraBoundary::reportMilestone(uint32_t milestoneKey, const AgoraValueArray& fields) {
    if (!reportGateOpen(milestoneKey)) return AgoraSubmitOutcome{};  // Sec4.2.1: guaranteed no-op
    return submitReport(fields);
}

AgoraSubmitOutcome AgoraBoundary::reportEvent(const AgoraValueArray& fields) {
    // Sec4.2 Shape B: "no existence check beforehand" - straight to the
    // queue wrapper, which still applies its own readiness check.
    return submitReport(fields);
}

AgoraSubmitOutcome AgoraBoundary::submitReport(const AgoraValueArray& fields) {
    // Sec4.1 step1: the sequence-number generator fires unconditionally,
    // even on every later gate failure below (CONFIRMED: it is the very
    // first instruction, before the readiness check).
    const uint64_t seq = backend_.nextSequenceNumber();

    // Sec4.1 step2: "if the master readiness flag is not strictly greater
    // than 0, return failure immediately."
    if (readiness_ != AgoraReadiness::Ready) return AgoraSubmitOutcome{};

    // Sec4.1 step6/step7: "if the caller supplied a value-array pointer
    // and a field count of at least 3" -> success path; otherwise ->
    // degenerate completion (callback code 0x200, returns 0). The real
    // size/CRC/encode checks inside step6's success branch are Hydra-
    // internal (not characterized to algorithm level, Sec0's own scope
    // limit) and are not reproduced or faked here - this library has
    // nothing real to hand off to Hydra's own transport layer anyway
    // (Sec4.3: out of scope past 0x00401b30).
    if (fields.size() < 3) return AgoraSubmitOutcome{};

    backend_.recordSubmittedReport(fields);
    AgoraSubmitOutcome outcome;
    outcome.queued = true;
    outcome.sequenceNumber = seq;
    return outcome;
}

AgoraHttpOutcome AgoraBoundary::handleHttpResponse(int32_t statusCode) {
    if (statusCode == 200) {
        return AgoraHttpOutcome::Ok;  // Sec5.1: decode step - stubbed, no real body
    }
    if (statusCode >= 400 && statusCode <= 499) {
        // Sec5.1: "the master readiness flag is unconditionally set to
        // 0xffffffff" - permanently, for the rest of the process (see
        // init()'s own one-shot contract above).
        readiness_ = AgoraReadiness::NotReady;
        return AgoraHttpOutcome::ClientError4xx;
    }
    // Sec5.1: 5xx / anything else -> reset-and-continue, no readiness
    // change.
    return AgoraHttpOutcome::Other;
}

}  // namespace sr3agora
