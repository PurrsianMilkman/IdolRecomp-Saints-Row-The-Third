#pragma once

// sr3agora/agora_backend.h - the Agora Games "Hydra SDK" / GDS telemetry
// call-boundary BACKEND (spec-agora-hydra-boundary.md, "Saints Row: The
// Third - Agora Games 'Hydra SDK' / GDS Telemetry Call Boundary (Game Side
// Only)"), following the same real-contract/injected-null-backend split
// this project already uses for sr3steam (steam_backend.h/steam_boundary.h)
// and sr3audioruntime (WwiseBackend/NullWwiseBackend).
//
// Per the task brief's own explicit scope decision (relayed from the user,
// restated in the spec's own header): "boundary only, null backend - the
// real servers (sr3.gds.thqconnect.com, sr3.hydra.agoragames.com) are dead."
// Unlike sr3steam/sr3physics/sr3audioruntime, there is no real SDK or
// server for Team B to eventually link against here - NullAgoraBackend
// below is not a placeholder for a future real backend, it IS the backend,
// permanently. Spec Sec5.2 (point 4) states this plainly: "a reimplementation
// or no-op stub of this boundary does not need to simulate any server
// behaviour at all to be faithful to what the shipped game actually does
// when the real backend is unreachable - it only needs to let the existing
// gates continue failing closed exactly as they already do." That is
// exactly what NullAgoraBackend's own defaults do.
//
// AgoraBackend is the boundary interface: the handful of CONFIRMED external
// calls the game-side wrapper functions make (Sec2.2 steps 1/3/4/5, Sec4.1
// step1/step6) that this library's orchestration logic (agora_boundary.h)
// cannot itself decide. AgoraBoundary is the GAME side; it calls these
// methods in exactly the order and shape Sec2/Sec3/Sec4 state.
//
// WHAT IS DELIBERATELY NOT ON THIS INTERFACE (OPEN, carried forward per this
// project's no-invented-fixes policy - the exact same discipline
// steam_backend.h's own banner applies to Steam's pre-init exit() gate):
//   - The master config flag gate (Sec2.1 step1, 0x013123d1) and the four
//     config-override arguments (Sec2.1 step6) - writer/source untraced
//     (OPEN items 1-2). Never touches the real init contract's own
//     observable success/failure shape (Sec2.2), so modelling it would add
//     a branch this file has no CONFIRMED trigger condition for.
//   - The post-init hook (Sec2.1 step8, 0x00710560) - into game code, not
//     Hydra's (OPEN item 3).
//   - The three apparent debug/console commands and their registration
//     (Sec2.1 step5, Sec6) - zero confirmed call sites anywhere, the exact
//     "no genuine use by this method" shape already found for Steam's own
//     SteamAPI_RestartAppIfNecessary.
//   - Shutdown's own untraced helper calls (Sec3.2, 0x00401a30/0x00402900,
//     OPEN items 7-8) - no observable effect beyond what Sec3.2's own
//     CONFIRMED session-sentinel reset already states (modelled directly
//     in agora_boundary.h's shutdown(), not through this interface).
//   - The HTTP response state machine's own dispatcher (Sec5.1) and the
//     connected-flag's one write site's own trigger (Sec4.2 Shape B, OPEN
//     item 12) - both are exposed as directly test-settable STATE on
//     AgoraBoundary itself (setConnected/handleHttpResponse), not as
//     backend calls, since nothing CONFIRMED says what drives either one.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "sr3agora/agora_value.h"

namespace sr3agora {

// Sec2.2 step3 (CONFIRMED): the client-identity helper derives its result
// from (a) the first Ethernet/Wi-Fi network adapter's 6-byte MAC address and
// (b) a call through the EXACT SAME SteamUser()/GetSteamID() vtable slot
// spec-steamworks-boundary.md's own Sec2.2 step8 already documents - "a
// genuine, confirmed cross-middleware dependency... not a coincidence of
// address proximity." Modelled as a single backend call rather than a hard
// link to sr3steam (this library stays link-independent, matching the
// project's current per-boundary-library structure - sr3luahost does not
// link sr3steam either); nothing CONFIRMED in this spec's own scope ever
// reads these two fields back out again (the boundary into Hydra's internal
// queue machinery that WOULD consume them, Sec2.1 step4, is explicitly out
// of scope), so this struct exists only so call-order/call-count fidelity
// (did derivation happen, exactly once, before the two failure helpers) is
// testable - not because any later CONFIRMED branch depends on its values.
struct AgoraClientIdentity {
    std::array<uint8_t, 6> mac{};
    uint64_t steamId = 0;
};

// The boundary interface. Every method is cited to its own spec section.
class AgoraBackend {
public:
    virtual ~AgoraBackend() = default;

    // Sec2.2 step3.
    virtual AgoraClientIdentity deriveClientIdentity() = 0;

    // Sec2.2 step4 (0x00402880): "if this helper returns nonzero, return
    // failure immediately - a real, reachable failure branch (not traced
    // further what specifically can make it fail; OPEN item 4)." Returns
    // true to signal "fail."
    virtual bool initFailureHelperA() = 0;
    // Sec2.2 step5 (0x004018b0): the second, likewise-untraced failure
    // branch (OPEN item 5). Returns true to signal "fail."
    virtual bool initFailureHelperB() = 0;

    // Sec4.1 step1: "InterlockedExchangeAdd(&0x013521c8, 1) - a thread-safe
    // sequence-number generator; the fetched value becomes this call's own
    // request ID." Single-threaded here (no real concurrency to guard).
    virtual uint64_t nextSequenceNumber() = 0;

    // Sec4.1 step6's success path: "store the encoded buffer and size into
    // the record, call 0x00401b30 (hands the record to Hydra's own
    // transport/queue - not traced past this boundary, out of scope)."
    // This call stands in for that out-of-scope hand-off; the null backend
    // has no real transport to hand anything to, so it only records the
    // submission for test inspection.
    virtual void recordSubmittedReport(const AgoraValueArray& fields) = 0;

    // Ordered log of every call made, for asserting Sec2/Sec4 call-order
    // fidelity against a null backend (same convention as sr3steam's
    // SteamBackend::callLog()).
    virtual const std::vector<std::string>& callLog() const = 0;
};

// NullAgoraBackend: the permanent backend (see file banner - there is no
// "real" backend this stands in for, unlike sr3steam's NullSteamBackend).
// Defaults match the shipped game's own observed behaviour against a dead
// server exactly: identity derivation always "succeeds" (it has no failure
// mode of its own - Sec2.2 step3 never branches on it), both untraced
// failure helpers default to "does not fail" (since nothing in this pass
// ever traced a path that makes them fail), each individually forceable to
// fail so the real branch a future binding would need to satisfy stays
// exercisable - the identical discipline sr3steam's own NullSteamBackend
// applies to SteamAPI_Init.
class NullAgoraBackend : public AgoraBackend {
public:
    NullAgoraBackend() = default;

    AgoraClientIdentity deriveClientIdentity() override;
    bool initFailureHelperA() override;
    bool initFailureHelperB() override;
    uint64_t nextSequenceNumber() override;
    void recordSubmittedReport(const AgoraValueArray& fields) override;

    const std::vector<std::string>& callLog() const override { return log_; }

    // --- Test-only configuration (never consulted by real behaviour) ---
    void setClientIdentity(const AgoraClientIdentity& identity) { identity_ = identity; }
    void setForceInitFailureA(bool fail) { forceFailureA_ = fail; }
    void setForceInitFailureB(bool fail) { forceFailureB_ = fail; }

    size_t submittedReportCount() const { return submittedReports_.size(); }
    const std::vector<AgoraValueArray>& submittedReports() const { return submittedReports_; }

private:
    void log(const std::string& entry) { log_.push_back(entry); }

    std::vector<std::string> log_;
    AgoraClientIdentity identity_{};
    bool forceFailureA_ = false;
    bool forceFailureB_ = false;
    uint64_t nextSequence_ = 0;
    std::vector<AgoraValueArray> submittedReports_;
};

}  // namespace sr3agora
