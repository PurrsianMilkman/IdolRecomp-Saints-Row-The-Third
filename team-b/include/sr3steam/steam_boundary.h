#pragma once

// sr3steam/steam_boundary.h - SteamBoundary: the GAME side of the
// Steamworks call-boundary contract (spec-steamworks-boundary.md), built
// against the SteamBackend interface (steam_backend.h). See that header's
// own doc comment for the backend/null-backend split this follows (the
// sr3audioruntime WwiseBackend/NullWwiseBackend precedent, matched here).
//
// Implements:
//   Sec2.2 - SteamAPI_Init's exact success/failure contract, including the
//     "silent skip" on failure (THE behaviour this project's own host
//     actually exercises on every launch, since no real Steam client is
//     present in this clean-room dev/test environment).
//   Sec2.3 - the generic LIFO shutdown-hook registry SteamAPI_Shutdown is
//     registered into (never called directly - ShutdownRegistry below).
//   Sec2.5 / Sec4.1 - the CCallback<T>-shaped register/unregister mechanism,
//     reused for both of this spec's confirmed callback ids
//     (P2PSessionRequest_t, LobbyDataUpdate_t).
//   Sec3 - the mandatory per-frame RunCallbacks pump, kept separate from the
//     optional request-queue bookkeeping cluster (Sec3 step2 / Sec4.2),
//     which Sec3's own closing line says a real binding does NOT need to
//     reproduce.
//   Sec4.2, Sec4.3 - the request-queue and cloud-storage on-demand clusters,
//     to the extent Sec4 gives CONFIRMED/HIGH detail to model meaningfully.
//   Sec3 step5 / Sec5.2 - the chat-overlay call SHAPE only (the gating
//     condition is OPEN, see below).
//
// NOT implemented (OPEN, insufficient CONFIRMED detail - Sec6 - carried
// forward rather than invented, per this project's no-invented-fixes
// policy):
//   - The pre-init exit() gate (Sec2.1 / Sec2.2 step2) - a SEPARATE,
//     non-Steam gate; the spec's own "Contract summary for a real-SDK
//     binding" (Sec2.2) omits it entirely, and it never touches any of the
//     17 imports.
//   - Sec2.2 step3's unrelated-subsystem registration and step11's tail
//     helpers (0x00d449d0, 0x00d1de80, 0x0101b5b0) - explicitly not
//     Steam-specific (Sec6 items 2-4).
//   - Sec3 steps 3/4 (the connection-phase state dword 0x024d6290, the
//     generic poll 0x005bae10) - these decide WHEN Sec3 step5's auto-open-
//     overlay check fires; OPEN (Sec6 items 8/9). openChatOverlayWithFriend()
//     below exposes the CONFIRMED call shape itself; the game decides when
//     to invoke it, exactly as the "real contract, null/injected-backend"
//     pattern intends.
//   - Sec4.1's iteration loop (0x0086e0d0, cap 0x15e0=5600 but "not traced
//     to field level") and 3-way dispatch (0x0086e230, OPEN item 10);
//     Sec4.4's SteamApps/SteamUtils/SteamUserStats/SteamNetworking
//     clusters (OPEN items 13/14) - none traced past "which imports are
//     touched"; nothing to model without inventing behaviour.
//   - SteamAPI_RegisterCallResult/UnregisterCallResult's real async
//     call-result flow (OPEN item 1's register/unregister count
//     asymmetry) - the two free functions exist on SteamBackend for
//     completeness (2 of the 17 imports) but no cluster here calls them.

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "sr3steam/steam_backend.h"

namespace sr3steam {

// Sec2.3: the generic LIFO cleanup-on-exit list SteamAPI_Shutdown's thunk
// address is appended to (0x02a07218, counter at 0x02a07708) - CONFIRMED
// mechanism, explicitly shared with other, non-Steam subsystems (0x00870320
// registers into the identical list earlier in the same boot sequence).
// Modelled generically here since the ordering guarantee itself ("last
// registered, first called") is exactly what Sec2.3's contract requires a
// real binding to reproduce, and is the reason "no flat CALL-site scan ever
// finds a direct call to SteamAPI_Shutdown - there isn't one." runAll()
// stands in for the walker at 0x00dc4410; it does not call the CRT's own
// __exit afterward (out of scope).
class ShutdownRegistry {
public:
    using Hook = std::function<void()>;

    void push(const std::string& name, Hook hook) { entries_.push_back({name, std::move(hook)}); }

    // Walks the list back-to-front (LIFO) - "after every cleanup entry
    // registered later than it" (Sec2.3) - calling each hook once, then
    // clears the list (process-exit shaped: it only ever runs once).
    void runAll() {
        for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
            callLog_.push_back(it->name);
            if (it->hook) it->hook();
        }
        entries_.clear();
    }

    size_t pendingCount() const { return entries_.size(); }
    const std::vector<std::string>& callOrderLog() const { return callLog_; }

private:
    struct Entry {
        std::string name;
        Hook hook;
    };
    std::vector<Entry> entries_;
    std::vector<std::string> callLog_;
};

// Sec2.5: the shape of the public SDK's header-only CCallback<T> helper
// (constructor registers, destructor unregisters) - HIGH CONFIDENCE by
// structure. Reused for both of this spec's confirmed callback ids
// (kP2PSessionRequestCallbackId, Sec2.5; kLobbyDataUpdateCallbackId,
// Sec4.1), since both registration sites (0x00870b20, 0x0086e1f0) follow
// the identical 4-step shape Sec2.5's own numbered list gives:
//   1. cache the unregister/register accessors (modelled as direct calls
//      on the backend, not cached registers - that caching has no
//      observable effect to reproduce);
//   2. if already registered, unregister first;
//   3. set the dispatch-function-pointer field - NOT modelled: the
//      dispatch target is this executable's own handler, and Sec2.5's own
//      text says "is not itself part of the contract Team B needs to
//      reproduce, only the registration/unregistration shape is";
//   4. push the callback id, push the object, call the register accessor.
class SteamCallbackRegistration {
public:
    explicit SteamCallbackRegistration(int32_t callbackId) : callbackId_(callbackId) {}

    void registerWith(SteamBackend& backend, void* callbackObj) {
        if (registered_) backend.steamAPI_UnregisterCallback(callbackObj);  // Sec2.5 step2
        backend.steamAPI_RegisterCallback(callbackObj, callbackId_);        // Sec2.5 step4
        registered_ = true;
    }

    void unregisterWith(SteamBackend& backend, void* callbackObj) {
        if (!registered_) return;
        backend.steamAPI_UnregisterCallback(callbackObj);
        registered_ = false;
    }

    bool isRegistered() const { return registered_; }
    int32_t callbackId() const { return callbackId_; }

private:
    int32_t callbackId_;
    bool registered_ = false;
};

// Sec4.3, 0x00b95eb0: the existence-then-size query pair's result.
struct CloudFileQueryResult {
    bool exists = false;
    int32_t size = 0;
};

// SteamBoundary - the GAME side of the contract. One instance per process,
// matching Sec2.2's "called from exactly one site ... a single,
// unconditional, once-per-process boot call."
class SteamBoundary {
public:
    explicit SteamBoundary(SteamBackend& backend) : backend_(backend) {}

    // Sec2.2: the exact init sequence. Returns the same bool SteamAPI_Init
    // itself returned.
    //
    // - If a PRIOR call already SUCCEEDED, this is a no-op that returns
    //   true again without calling the backend a second time (Sec2.2
    //   step1's guard byte - step4 stores Init's own return value into
    //   that SAME byte, so only a successful prior call leaves the guard
    //   "already set"; a prior FAILURE leaves it clear, so a later retry
    //   is not refused - this matches the function's own literal step
    //   order, not just its "called once" common case).
    // - On failure: returns false having done nothing else at all - no
    //   SteamUser()/SteamClient(), no callback registration, no
    //   shutdown-hook registration (Sec2.2 step5, the headline finding:
    //   "no retry, no dialog, no exit").
    bool init(void* warningHook = nullptr);

    bool steamApiInitSucceeded() const { return initGuard_; }
    uint64_t lastSteamID() const { return lastSteamId_; }
    bool standingCallbackRegistered() const { return p2pCallback_.isRegistered(); }

    // Sec3 step1: "the entire Steamworks-mandated part of the per-frame
    // contract." Intended to be called once per frame, regardless of
    // whether init() ever succeeded - the game keeps running either way
    // (Sec2.2 step5), and nothing in Sec3 gates this call on init's result.
    void runCallbacks() { backend_.steamAPI_RunCallbacks(); }

    // Sec2.3: the LIFO list SteamAPI_Shutdown was (conditionally)
    // registered into by init(). The "game" calls
    // shutdownRegistry().runAll() once, at process exit - there is
    // deliberately no separate SteamBoundary::shutdown() method, because
    // the real contract has no direct call site either (Sec2.3's own
    // point: "this explains why no flat CALL-site scan ever finds a direct
    // call to SteamAPI_Shutdown - there isn't one").
    ShutdownRegistry& shutdownRegistry() { return shutdownRegistry_; }

    // -----------------------------------------------------------------
    // Sec4.2 / Sec3 step2: the request-queue bookkeeping cluster. NOT part
    // of the mandatory per-frame contract (Sec3's own closing line); kept
    // separate from runCallbacks() on purpose.
    // -----------------------------------------------------------------
    // 0x00870700-shaped writer: sets the first pending-request slot pair.
    void requestTypeA(uint32_t a, uint32_t b) {
        slotA_ = {a, b};
        slotASet_ = true;
    }
    // 0x00870880-shaped writer: sets the second pending-request slot pair.
    void requestTypeB(uint32_t a, uint32_t b) {
        slotB_ = {a, b};
        slotBSet_ = true;
    }

    // Sec3 step2's own branching, applied in order:
    //   - neither slot set -> no-op ("skip to step 3");
    //   - second slot set -> dispatch through `dispatchEvent` (stands in
    //     for the generic event-object pipeline, Sec6 OPEN item7 - not
    //     resolved to a Lua-visible event name), then clear it;
    //   - otherwise (so the first slot must be set) -> if the one-shot
    //     flag is clear, call SteamMatchmaking() vtable+0x6c with the
    //     first slot's two dwords, then set the flag so this fires only
    //     once per process. The first slot's own value is NOT cleared -
    //     only the one-shot flag gates re-firing; the spec does not state
    //     the slot itself is cleared by this step.
    using DispatchEventFn = std::function<void(uint32_t, uint32_t)>;
    void drainPendingRequests(const DispatchEventFn& dispatchEvent);

    bool matchmakingOneShotFired() const { return oneShotFired_; }

    // -----------------------------------------------------------------
    // Sec4.1: matchmaking/lobby standing callback (LobbyDataUpdate_t).
    // -----------------------------------------------------------------
    void registerLobbyDataUpdateCallback() { lobbyCallback_.registerWith(backend_, &lobbyCallbackObj_); }
    void unregisterLobbyDataUpdateCallback() { lobbyCallback_.unregisterWith(backend_, &lobbyCallbackObj_); }
    bool lobbyCallbackRegistered() const { return lobbyCallback_.isRegistered(); }

    // -----------------------------------------------------------------
    // Sec4.3: the cloud-storage cluster.
    // -----------------------------------------------------------------
    // 0x00b95eb0 (HIGH): existence query then size/info query.
    CloudFileQueryResult cloudStorageFileQuery(const std::string& fileName);
    // 0x00b96050 (CONFIRMED): explicit SteamUser() null-check, returns
    // error code 6 on the null branch instead of dereferencing it - "the
    // one place in the whole census where the game's own code is written
    // expecting the interface accessor to sometimes come back unavailable
    // at the individual-call level, not only at startup." What the
    // operation itself does beyond this guard is OPEN (Sec6 item 12); the
    // success branch is a stub return only.
    int32_t cloudStorageGuardedOperation();

    // -----------------------------------------------------------------
    // Sec3 step5 / Sec5.2: the chat-overlay call SHAPE. The gating
    // condition (a session/lobby-owner check, Sec3 steps 3/4) is OPEN
    // (Sec6 items 8/9) and not modelled here - the caller decides when to
    // invoke this, exactly as this project's "real contract, injected-
    // backend" pattern intends.
    // -----------------------------------------------------------------
    void openChatOverlayWithFriend(uint64_t friendSteamId) {
        backend_.activateGameOverlayToUser(friendSteamId, kChatOverlayDialogName);
    }

private:
    SteamBackend& backend_;
    bool initGuard_ = false;  // 0x024d6800: holds Init()'s own last return value
    uint64_t lastSteamId_ = 0;

    ShutdownRegistry shutdownRegistry_;
    SteamCallbackRegistration p2pCallback_{kP2PSessionRequestCallbackId};
    SteamCallbackRegistration lobbyCallback_{kLobbyDataUpdateCallbackId};
    // Distinct dummy addresses stand in for the two distinct persistent
    // callback objects (Sec2.2's own 0x024d6330, and Sec4.1's own object);
    // never dereferenced by this class, used only as an opaque identity
    // token the backend's test double can log.
    int p2pCallbackObj_ = 0;
    int lobbyCallbackObj_ = 0;

    std::pair<uint32_t, uint32_t> slotA_{0, 0};
    std::pair<uint32_t, uint32_t> slotB_{0, 0};
    bool slotASet_ = false;
    bool slotBSet_ = false;
    bool oneShotFired_ = false;  // 0x024d6295
};

}  // namespace sr3steam
