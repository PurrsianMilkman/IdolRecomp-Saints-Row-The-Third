// Synthetic tests for the Agora Games "Hydra SDK" / GDS telemetry call
// boundary - spec-agora-hydra-boundary.md Sec1 (the GDS_value_* type),
// Sec2 (init), Sec3 (shutdown), Sec4 (on-demand reporting), Sec5 (HTTP
// response dispatch / the dead-backend consequence). Every expected value
// below is a rule quoted from those sections (cited per block);
// AgoraBackend/NullAgoraBackend is this project's own injected-backend
// stand-in - per the task's own scope decision there is no real backend to
// build toward later (the real servers are dead), so NullAgoraBackend's
// defaults ARE the permanent behaviour, not a placeholder.
#include <iostream>
#include <string>
#include <vector>

#include "sr3agora/agora_backend.h"
#include "sr3agora/agora_boundary.h"
#include "sr3agora/agora_value.h"

using namespace sr3agora;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

AgoraValueArray makeFields(size_t count) {
    AgoraValueArray fields;
    for (size_t i = 0; i < count; ++i) {
        AgoraValue v;
        v.setInt32(static_cast<int32_t>(i));
        fields.push_back(v);
    }
    return fields;
}

// ===================================================================
// Sec1.1/Sec1.4: the nine set_<kind>/get_<kind> pairs, each the "store the
// tag + raw value, nothing else" shape, plus the default-null state.
// ===================================================================
void testValueSettersAndDefaultNull() {
    AgoraValue v;
    CHECK(v.isNull());
    CHECK(v.kind() == AgoraValueKind::Null);

    v.setBool(true);
    CHECK(v.kind() == AgoraValueKind::Bool);
    CHECK(v.getBool() == true);

    v.setInt32(-42);
    CHECK(v.kind() == AgoraValueKind::Int32);
    CHECK(v.getInt32() == -42);

    v.setInt64(1234567890123LL);
    CHECK(v.getInt64() == 1234567890123LL);

    v.setFloat32(3.5f);
    CHECK(v.getFloat32() == 3.5f);

    v.setFloat64(2.5);
    CHECK(v.getFloat64() == 2.5);

    v.setCrc16(0xabcd);
    CHECK(v.getCrc16() == 0xabcd);

    v.setCrc32(0xdeadbeef);
    CHECK(v.kind() == AgoraValueKind::Crc32);
    CHECK(v.getCrc32() == 0xdeadbeef);

    std::vector<uint8_t> data = {1, 2, 3, 4};
    v.setData(data);
    CHECK(v.kind() == AgoraValueKind::Data);
    CHECK(v.getData() == data);

    v.setText("hello");
    CHECK(v.kind() == AgoraValueKind::Text);
    CHECK(v.getText() == "hello");

    v.setIsNull();
    CHECK(v.isNull());
}

// ===================================================================
// Sec1.3: set_text's own real logic - "strlen-or-explicit-length logic,
// clamped to 0xffffff bytes." The explicit-length branch, and the clamp
// itself (built against a source buffer actually large enough to exceed
// the clamp, so the test is real rather than vacuous).
// ===================================================================
void testValueTextLengthAndClamp() {
    AgoraValue v;
    // Explicit-length branch: shorter than the string's own length.
    v.setText("hello world", 5);
    CHECK(v.getText() == "hello");

    // strlen branch (explicitLength == -1, the default): full string.
    v.setText("hello world");
    CHECK(v.getText() == "hello world");

    // The 0xffffff clamp itself, against a real oversized buffer.
    const size_t oversized = AgoraValue::kMaxTextBytes + 16;
    std::string big(oversized, 'a');
    v.setText(big);
    CHECK(v.getText().size() == AgoraValue::kMaxTextBytes);

    // Same clamp via the explicit-length branch.
    AgoraValue v2;
    v2.setText(big, static_cast<int32_t>(oversized));
    CHECK(v2.getText().size() == AgoraValue::kMaxTextBytes);
}

// ===================================================================
// Sec2.1 step3 / Sec2.2's own "Contract summary": the "active" flag is set
// BEFORE the real init call runs and is NEVER rolled back on failure - the
// headline divergence from sr3steam's own SteamAPI_Init guard (which DOES
// allow a retry after a failure). init() only ever genuinely runs once;
// every later call replays the first call's own cached result, even after
// the backend's force-fail hooks are cleared.
// ===================================================================
void testInitActiveFlagNeverRolledBackNoRetry() {
    NullAgoraBackend backend;
    backend.setForceInitFailureA(true);
    AgoraBoundary boundary(backend);

    CHECK(boundary.init() == false);
    CHECK(boundary.isActive());
    CHECK(boundary.readinessState() == AgoraReadiness::NotReady);
    CHECK(!boundary.isReady());

    // Clear the forced failure - unlike sr3steam, this must NOT enable a
    // real retry: the backend's own helpers must not be invoked again.
    backend.setForceInitFailureA(false);
    const size_t logSizeBeforeRetry = backend.callLog().size();
    CHECK(boundary.init() == false);  // still replays the FIRST call's result
    CHECK(backend.callLog().size() == logSizeBeforeRetry);  // no new backend calls at all
    CHECK(boundary.readinessState() == AgoraReadiness::NotReady);
}

// ===================================================================
// Sec2.2 steps 1-6: the full success path, in order - derive identity
// (step3), both failure helpers checked and passing (step4/step5), then
// readiness -> Ready (step6).
// ===================================================================
void testInitSuccessPathOrderAndContract() {
    NullAgoraBackend backend;
    AgoraClientIdentity identity;
    identity.mac = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    identity.steamId = 0x0123456789abcdefULL;
    backend.setClientIdentity(identity);
    AgoraBoundary boundary(backend);

    CHECK(boundary.init() == true);
    CHECK(boundary.isActive());
    CHECK(boundary.readinessState() == AgoraReadiness::Ready);
    CHECK(boundary.isReady());

    std::vector<std::string> expected = {
        "Hydra::DeriveClientIdentity(0x004023a0)",
        "Hydra::InitFailureHelperA(0x00402880)",
        "Hydra::InitFailureHelperB(0x004018b0)",
    };
    CHECK(backend.callLog() == expected);

    // A second call is a no-op: no new backend calls, same cached result.
    CHECK(boundary.init() == true);
    CHECK(backend.callLog() == expected);
}

// ===================================================================
// Sec2.2 step5 (the second untraced failure helper) is only reached if
// step4 (the first) passes - matching the exact instruction order.
// ===================================================================
void testInitFailureHelperOrder() {
    NullAgoraBackend backend;
    backend.setForceInitFailureB(true);
    AgoraBoundary boundary(backend);

    CHECK(boundary.init() == false);
    std::vector<std::string> expected = {
        "Hydra::DeriveClientIdentity(0x004023a0)",
        "Hydra::InitFailureHelperA(0x00402880)",
        "Hydra::InitFailureHelperB(0x004018b0)",
    };
    CHECK(backend.callLog() == expected);
    CHECK(boundary.readinessState() == AgoraReadiness::NotReady);
}

// ===================================================================
// Sec4.2.1 / Sec5.2 point1: "every one of these 63 call sites is a
// guaranteed, two-instruction, side-effect-free no-op" by default - a
// fresh process starts with no session (-1,-1) and connected == false
// (CONFIRMED PE zero-init guarantee). reportMilestone must be a pure no-op:
// no backend call, nothing recorded.
// ===================================================================
void testShapeAGateClosedByDefault() {
    NullAgoraBackend backend;
    AgoraBoundary boundary(backend);
    CHECK(boundary.init() == true);
    CHECK(!boundary.hasSession());
    CHECK(!boundary.isConnected());

    AgoraSubmitOutcome outcome = boundary.reportMilestone(0xafb6ad38u, makeFields(5));
    CHECK(outcome.queued == false);
    CHECK(backend.submittedReportCount() == 0);
    // nextSequenceNumber() is never reached either - the gate closes
    // before the queue wrapper is ever entered.
    std::vector<std::string> expected = {
        "Hydra::DeriveClientIdentity(0x004023a0)",
        "Hydra::InitFailureHelperA(0x00402880)",
        "Hydra::InitFailureHelperB(0x004018b0)",
    };
    CHECK(backend.callLog() == expected);
}

// ===================================================================
// Sec4.2.1: with a live session and connected == true, the gate opens
// exactly once per distinct key - "already reported" collapses into the
// identical no-op the "no session"/"not connected" branches use.
// ===================================================================
void testShapeADedupByKey() {
    NullAgoraBackend backend;
    AgoraBoundary boundary(backend);
    CHECK(boundary.init() == true);
    boundary.setSessionId(111, 222);
    boundary.setConnected(true);
    CHECK(boundary.hasSession());
    CHECK(boundary.isConnected());

    AgoraSubmitOutcome first = boundary.reportMilestone(42, makeFields(5));
    CHECK(first.queued == true);
    CHECK(backend.submittedReportCount() == 1);
    CHECK(boundary.reportedMilestoneCount() == 1);

    // Same key again: dedup gate closes it, even though session/connected
    // are unchanged.
    AgoraSubmitOutcome second = boundary.reportMilestone(42, makeFields(5));
    CHECK(second.queued == false);
    CHECK(backend.submittedReportCount() == 1);  // unchanged

    // A DIFFERENT key still opens normally.
    AgoraSubmitOutcome third = boundary.reportMilestone(43, makeFields(5));
    CHECK(third.queued == true);
    CHECK(backend.submittedReportCount() == 2);
    CHECK(boundary.reportedMilestoneCount() == 2);
}

// ===================================================================
// Sec4.2 Shape B: "no existence check beforehand" - reportEvent is NOT
// gated by session/connected state at all, only by the queue wrapper's own
// readiness check (Sec4.1 step2), which applies identically to both
// shapes.
// ===================================================================
void testShapeBBypassesExistenceGate() {
    NullAgoraBackend backend;
    AgoraBoundary boundary(backend);

    // Before init(): readiness is NotReady, so even Shape B is refused -
    // but NOT because of session/connected state (both still at their
    // defaults, demonstrating the gate truly is not consulted here).
    CHECK(!boundary.hasSession());
    CHECK(!boundary.isConnected());
    AgoraSubmitOutcome beforeInit = boundary.reportEvent(makeFields(5));
    CHECK(beforeInit.queued == false);

    CHECK(boundary.init() == true);
    // Still no session, still not connected - Shape B succeeds anyway.
    CHECK(!boundary.hasSession());
    CHECK(!boundary.isConnected());
    AgoraSubmitOutcome afterInit = boundary.reportEvent(makeFields(5));
    CHECK(afterInit.queued == true);
    CHECK(backend.submittedReportCount() == 1);
}

// ===================================================================
// Sec4.1 step1/step6-7: the sequence-number generator fires on every call
// (even a degenerate one), and a successful submission returns it; fewer
// than 3 fields takes the degenerate completion path (queued == false,
// nothing handed to the backend).
// ===================================================================
void testSequenceNumberAndFieldCountDegeneracy() {
    NullAgoraBackend backend;
    AgoraBoundary boundary(backend);
    CHECK(boundary.init() == true);

    AgoraSubmitOutcome tooFew = boundary.reportEvent(makeFields(2));
    CHECK(tooFew.queued == false);
    CHECK(backend.submittedReportCount() == 0);

    AgoraSubmitOutcome enough = boundary.reportEvent(makeFields(3));
    CHECK(enough.queued == true);
    CHECK(backend.submittedReportCount() == 1);

    AgoraSubmitOutcome again = boundary.reportEvent(makeFields(4));
    CHECK(again.queued == true);
    // Sequence numbers strictly increase across every call into the queue
    // wrapper, including the earlier degenerate one (Sec4.1 step1 fires
    // before the readiness/field-count checks).
    CHECK(again.sequenceNumber > enough.sequenceNumber);
}

// ===================================================================
// Sec5.1: status 400-499 permanently kills readiness; combined with
// Sec2.2's own one-shot init() contract, this is PERMANENT for the rest of
// the boundary's life - a later init() call is still the cached no-op, so
// readiness never comes back (Sec5.2 point3/Sec0 point5's own "permanently
// disables all further reporting for the rest of the process").
// ===================================================================
void testHttp4xxPermanentlyKillsReadiness() {
    NullAgoraBackend backend;
    AgoraBoundary boundary(backend);
    CHECK(boundary.init() == true);
    CHECK(boundary.isReady());

    AgoraHttpOutcome outcome = boundary.handleHttpResponse(404);
    CHECK(outcome == AgoraHttpOutcome::ClientError4xx);
    CHECK(!boundary.isReady());
    CHECK(boundary.readinessState() == AgoraReadiness::NotReady);

    // Even a fully generic report (Shape B, no gate) is now refused.
    AgoraSubmitOutcome outcome2 = boundary.reportEvent(makeFields(5));
    CHECK(outcome2.queued == false);

    // A later init() call does NOT restore readiness - it is still the
    // permanent no-op from the FIRST call (Sec2.1 step3's own "active"
    // flag).
    CHECK(boundary.init() == true);  // replays the original cached success
    CHECK(boundary.readinessState() == AgoraReadiness::NotReady);  // still killed
}

// ===================================================================
// Sec5.1: 200 decodes (no readiness change); 5xx and anything else out of
// the 400-499 range reset-and-continue (no readiness change either).
// ===================================================================
void testHttp200AndOtherLeaveReadinessUnchanged() {
    NullAgoraBackend backend;
    AgoraBoundary boundary(backend);
    CHECK(boundary.init() == true);

    CHECK(boundary.handleHttpResponse(200) == AgoraHttpOutcome::Ok);
    CHECK(boundary.isReady());

    CHECK(boundary.handleHttpResponse(500) == AgoraHttpOutcome::Other);
    CHECK(boundary.isReady());

    CHECK(boundary.handleHttpResponse(399) == AgoraHttpOutcome::Other);  // just below the 4xx range
    CHECK(boundary.isReady());

    CHECK(boundary.handleHttpResponse(0) == AgoraHttpOutcome::Other);  // HIGH: connection-failure shape
    CHECK(boundary.isReady());
}

// ===================================================================
// Sec3.1/Sec3.2: shutdown resets session-id, connected, and the dedup
// registry to their fresh-process defaults and sets readiness to
// ShutDown - but does NOT reset the "active" flag (no instruction in
// Sec3.1's own listing touches it), so init() remains the permanent no-op.
// ===================================================================
void testShutdownResetsStateButNotActiveFlag() {
    NullAgoraBackend backend;
    AgoraBoundary boundary(backend);
    CHECK(boundary.init() == true);
    boundary.setSessionId(5, 6);
    boundary.setConnected(true);
    CHECK(boundary.reportMilestone(1, makeFields(5)).queued == true);
    CHECK(boundary.reportedMilestoneCount() == 1);

    boundary.shutdown();
    CHECK(boundary.readinessState() == AgoraReadiness::ShutDown);
    CHECK(!boundary.isReady());
    CHECK(!boundary.hasSession());
    CHECK(boundary.sessionId().first == -1 && boundary.sessionId().second == -1);
    CHECK(!boundary.isConnected());
    CHECK(boundary.reportedMilestoneCount() == 0);

    // The active flag is untouched: init() is STILL the permanent no-op
    // from the original successful call - no new backend calls, and
    // readiness stays ShutDown rather than going back to Ready.
    const size_t logSizeBeforeSecondInit = backend.callLog().size();
    CHECK(boundary.init() == true);  // replays the original cached result
    CHECK(backend.callLog().size() == logSizeBeforeSecondInit);
    CHECK(boundary.readinessState() == AgoraReadiness::ShutDown);
}

// ===================================================================
// Sec3.1: "if active flag == 0: return immediately" - shutdown() before
// any init() call is a pure no-op.
// ===================================================================
void testShutdownNoopIfNeverInitialized() {
    NullAgoraBackend backend;
    AgoraBoundary boundary(backend);
    boundary.shutdown();
    CHECK(boundary.readinessState() == AgoraReadiness::NotReady);  // unchanged default
    CHECK(backend.callLog().empty());
}

// ===================================================================
// Sec3.2 step1: "if readiness == 0: skip to ret (already shut down)" - a
// second shutdown() call is a no-op too.
// ===================================================================
void testShutdownNoopIfCalledTwice() {
    NullAgoraBackend backend;
    AgoraBoundary boundary(backend);
    CHECK(boundary.init() == true);
    boundary.setSessionId(1, 2);
    boundary.shutdown();
    CHECK(boundary.readinessState() == AgoraReadiness::ShutDown);

    boundary.setSessionId(9, 9);  // prove a second shutdown() does not touch this again
    boundary.shutdown();
    CHECK(boundary.sessionId().first == 9 && boundary.sessionId().second == 9);
    CHECK(boundary.readinessState() == AgoraReadiness::ShutDown);
}

}  // namespace

int main() {
    testValueSettersAndDefaultNull();
    testValueTextLengthAndClamp();
    testInitActiveFlagNeverRolledBackNoRetry();
    testInitSuccessPathOrderAndContract();
    testInitFailureHelperOrder();
    testShapeAGateClosedByDefault();
    testShapeADedupByKey();
    testShapeBBypassesExistenceGate();
    testSequenceNumberAndFieldCountDegeneracy();
    testHttp4xxPermanentlyKillsReadiness();
    testHttp200AndOtherLeaveReadinessUnchanged();
    testShutdownResetsStateButNotActiveFlag();
    testShutdownNoopIfNeverInitialized();
    testShutdownNoopIfCalledTwice();
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_sr3agora_test: all checks passed\n";
    return 0;
}
