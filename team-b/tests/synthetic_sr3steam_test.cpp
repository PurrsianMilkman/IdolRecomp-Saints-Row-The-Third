// Synthetic tests for the Steamworks call boundary - spec-steamworks-
// boundary.md Sec2 (init/shutdown), Sec3 (per-frame), Sec4 (on-demand
// clusters). Every expected value below is a rule quoted from those
// sections (cited per block); SteamBackend/NullSteamBackend is this
// project's own injected-backend stand-in, not a Steamworks SDK.
#include <iostream>
#include <string>
#include <vector>

#include "sr3steam/steam_backend.h"
#include "sr3steam/steam_boundary.h"

using namespace sr3steam;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

// ===================================================================
// Sec2.2 step5: SteamAPI_Init failure is a silent skip - the headline
// finding this task exists to get right. On false: return immediately,
// nothing past that point runs (no SteamUser()/SteamClient(), no callback
// registration, no shutdown-hook registration, no retry, no dialog, no
// exit), and the host must be able to keep running normally afterward.
// ===================================================================
void testInitSilentSkipOnFailure() {
    NullSteamBackend backend;
    backend.setForceInitFailure(true);
    SteamBoundary boundary(backend);

    const bool ok = boundary.init();
    CHECK(ok == false);
    CHECK(!boundary.steamApiInitSucceeded());
    CHECK(backend.initCalled());

    // Nothing else ran: no SteamUser()/SteamClient() call, no callback
    // registration, no SetWarningMessageHook, no shutdown-hook
    // registration - the call log holds exactly the one failed Init call.
    std::vector<std::string> expected = {"SteamAPI_Init()"};
    CHECK(backend.callLog() == expected);
    CHECK(!boundary.standingCallbackRegistered());
    CHECK(boundary.shutdownRegistry().pendingCount() == 0);
    CHECK(backend.lastWarningHook() == nullptr);

    // The host proceeds fine without Steam: the per-frame pump and the
    // on-demand clusters all still work with no crash and no special-
    // casing needed, exactly as the real game does when Steam never
    // initialized.
    boundary.runCallbacks();
    CHECK(backend.runCallbacksCount() == 1);

    // Init failing and SteamUser() availability are independent in the
    // stub DLL (Sec2.1: every accessor is its OWN always-non-null body,
    // unrelated to SteamAPI_Init's own body) - an on-demand cluster used
    // after a failed init still runs its own guard correctly rather than
    // crashing.
    CHECK(boundary.cloudStorageGuardedOperation() == 0);
}

// ===================================================================
// Sec2.2 step1/step4's own subtlety: the guard byte literally holds
// SteamAPI_Init's last return value (step4: "store the return value into
// the guard byte"). A prior SUCCESS makes init() a no-op on a repeat call;
// a prior FAILURE leaves the guard clear, so a later retry is not refused
// and genuinely re-invokes the backend.
// ===================================================================
void testInitGuardByteSubtlety() {
    NullSteamBackend backend;
    backend.setForceInitFailure(true);
    SteamBoundary boundary(backend);

    CHECK(boundary.init() == false);
    CHECK(backend.initCalled());

    // Retry after a failure: NOT refused - the backend is called again.
    backend.setForceInitFailure(false);
    CHECK(boundary.init() == true);
    CHECK(boundary.steamApiInitSucceeded());
    int initCallCount = 0;
    for (const auto& entry : backend.callLog())
        if (entry == "SteamAPI_Init()") ++initCallCount;
    CHECK(initCallCount == 2);

    // A further call now IS a no-op: the guard is set (last call
    // succeeded), so the backend's Init is not invoked a third time.
    CHECK(boundary.init() == true);
    initCallCount = 0;
    for (const auto& entry : backend.callLog())
        if (entry == "SteamAPI_Init()") ++initCallCount;
    CHECK(initCallCount == 2);  // unchanged
}

// ===================================================================
// Sec2.2 steps 6-10: the full success path, in order - SteamAPI_Init(),
// then (step7) the shutdown-hook registration (Sec2.3, observed via
// shutdownRegistry()'s pending count, not the backend's own call log since
// SteamAPI_Shutdown itself isn't invoked here), GetSteamID() (step8), the
// standing P2PSessionRequest_t callback registration (step9/Sec2.5), then
// SetWarningMessageHook() (step10).
// ===================================================================
void testInitSuccessPathOrderAndContract() {
    NullSteamBackend backend;
    backend.setSteamIDValue(0x0123456789abcdefULL);  // arbitrary synthetic test value
    SteamBoundary boundary(backend);

    int dummyHookToken = 0;
    void* hook = &dummyHookToken;
    CHECK(boundary.init(hook) == true);
    CHECK(boundary.steamApiInitSucceeded());
    CHECK(boundary.lastSteamID() == 0x0123456789abcdefULL);
    CHECK(boundary.standingCallbackRegistered());
    CHECK(backend.lastWarningHook() == hook);

    std::vector<std::string> expected = {
        "SteamAPI_Init()",
        "ISteamUser::GetSteamID()",
        "SteamAPI_RegisterCallback(id=" + std::to_string(kP2PSessionRequestCallbackId) + ")",
        "ISteamClient::SetWarningMessageHook()",
    };
    CHECK(backend.callLog() == expected);

    // Sec2.3: the shutdown hook was registered (only on this success
    // path), but SteamAPI_Shutdown itself has not been called yet - it is
    // only reachable through the LIFO walk at process exit.
    CHECK(boundary.shutdownRegistry().pendingCount() == 1);
    CHECK(!backend.shutdownCalled());
}

// ===================================================================
// Sec2.5: 1202 decimal (0x4b2) = k_iSteamNetworkingCallbacks(1200)+2 =
// P2PSessionRequest_t. Sec4.1: 505 decimal (0x1f9) =
// k_iSteamMatchmakingCallbacks(500)+5 = LobbyDataUpdate_t. Both numeric
// matches to Valve's own publicly published header enum values.
// ===================================================================
void testCallbackIdConstants() {
    CHECK(kP2PSessionRequestCallbackId == 1202);
    CHECK(kLobbyDataUpdateCallbackId == 505);
}

// ===================================================================
// Sec2.3: SteamAPI_Shutdown is registered into a generic LIFO cleanup
// list, not called directly - "last registered, first called." No init()
// call site exists for SteamBoundary::shutdown() at all; the only way to
// run the hook is through shutdownRegistry().runAll(), standing in for the
// process-exit walker at 0x00dc4410.
// ===================================================================
void testShutdownLifoOrdering() {
    // The registry itself, in isolation: three hooks pushed in order
    // a, b, c must run back-to-front: c, b, a.
    {
        ShutdownRegistry registry;
        std::vector<std::string> order;
        registry.push("a", [&] { order.push_back("a"); });
        registry.push("b", [&] { order.push_back("b"); });
        registry.push("c", [&] { order.push_back("c"); });
        registry.runAll();
        std::vector<std::string> expected = {"c", "b", "a"};
        CHECK(order == expected);
        CHECK(registry.callOrderLog() == expected);
        CHECK(registry.pendingCount() == 0);
    }

    // Through SteamBoundary: init() succeeds, registering
    // "SteamAPI_Shutdown" into the list; a later, unrelated subsystem's
    // hook registers AFTER it (the registry is explicitly a shared,
    // generic mechanism, Sec2.3's own framing - "the engine's generic
    // shutdown-hook registry, not something built only for Steam"). LIFO
    // means the later-registered hook runs FIRST, strictly before
    // SteamAPI_Shutdown.
    {
        NullSteamBackend backend;
        SteamBoundary boundary(backend);
        CHECK(boundary.init() == true);
        CHECK(boundary.shutdownRegistry().pendingCount() == 1);

        bool otherRan = false;
        boundary.shutdownRegistry().push("other_subsystem", [&] { otherRan = true; });
        CHECK(boundary.shutdownRegistry().pendingCount() == 2);

        boundary.shutdownRegistry().runAll();
        CHECK(otherRan);
        CHECK(backend.shutdownCalled());
        std::vector<std::string> expected = {"other_subsystem", "SteamAPI_Shutdown"};
        CHECK(boundary.shutdownRegistry().callOrderLog() == expected);
        CHECK(boundary.shutdownRegistry().pendingCount() == 0);
    }

    // Symmetric with the init-failure silent skip (Sec2.2 step5 / Sec2.3's
    // closing line): if init failed, nothing Steam-shaped was ever
    // registered, so there is nothing to run at process exit either.
    {
        NullSteamBackend backend;
        backend.setForceInitFailure(true);
        SteamBoundary boundary(backend);
        CHECK(boundary.init() == false);
        CHECK(boundary.shutdownRegistry().pendingCount() == 0);
        boundary.shutdownRegistry().runAll();
        CHECK(!backend.shutdownCalled());
    }
}

// ===================================================================
// Sec3 step1: "First instruction: call SteamAPI_RunCallbacks(). This is
// the entire Steamworks-mandated part of the per-frame contract." Pumped
// once per invocation, independent of init's own outcome.
// ===================================================================
void testRunCallbacksPerFramePump() {
    NullSteamBackend backend;
    SteamBoundary boundary(backend);

    boundary.runCallbacks();
    boundary.runCallbacks();
    boundary.runCallbacks();
    CHECK(backend.runCallbacksCount() == 3);
}

// ===================================================================
// Sec3 step2 / Sec4.2: the request-queue bookkeeping cluster's exact
// branching - neither slot set skips; the second slot set dispatches
// through the generic event pipeline and is consumed; otherwise the first
// slot's one-shot-gated SteamMatchmaking() vtable+0x6c call fires exactly
// once per process.
// ===================================================================
void testPendingRequestQueueCluster() {
    NullSteamBackend backend;
    SteamBoundary boundary(backend);

    // Neither slot set: no-op.
    bool dispatched = false;
    boundary.drainPendingRequests([&](uint32_t, uint32_t) { dispatched = true; });
    CHECK(!dispatched);
    CHECK(backend.callLog().empty());

    // Second slot set: dispatches, then is consumed (a later drain with
    // nothing re-set does not re-dispatch).
    boundary.requestTypeB(7, 8);
    uint32_t gotA = 0, gotB = 0;
    int dispatchCount = 0;
    boundary.drainPendingRequests([&](uint32_t a, uint32_t b) {
        gotA = a;
        gotB = b;
        ++dispatchCount;
    });
    CHECK(dispatchCount == 1 && gotA == 7 && gotB == 8);
    boundary.drainPendingRequests([&](uint32_t, uint32_t) { ++dispatchCount; });
    CHECK(dispatchCount == 1);  // not re-dispatched

    // First slot set, second not: fires SteamMatchmaking() vtable+0x6c
    // with the first slot's two dwords, exactly once (one-shot flag).
    CHECK(!boundary.matchmakingOneShotFired());
    boundary.requestTypeA(1, 2);
    boundary.drainPendingRequests(nullptr);
    CHECK(boundary.matchmakingOneShotFired());
    std::vector<std::string> expected = {"ISteamMatchmaking::+0x6c(1,2)"};
    CHECK(backend.callLog() == expected);

    // Re-requesting the first slot after the flag has fired must NOT
    // fire again - "set the flag so this fires only once."
    boundary.requestTypeA(99, 100);
    boundary.drainPendingRequests(nullptr);
    CHECK(backend.callLog() == expected);  // unchanged

    // When both slots are set in the same drain, the second slot takes
    // priority (the spec's own "otherwise" framing) - the matchmaking
    // call does not fire again even though slot A is also set.
    NullSteamBackend backend2;
    SteamBoundary boundary2(backend2);
    boundary2.requestTypeA(5, 6);
    boundary2.requestTypeB(10, 11);
    int dispatchCount2 = 0;
    boundary2.drainPendingRequests([&](uint32_t, uint32_t) { ++dispatchCount2; });
    CHECK(dispatchCount2 == 1);
    CHECK(!boundary2.matchmakingOneShotFired());
    for (const auto& entry : backend2.callLog()) CHECK(entry.find("Matchmaking") == std::string::npos);
}

// ===================================================================
// Sec4.1: the matchmaking/lobby standing callback (LobbyDataUpdate_t, id
// 505), following the identical CCallback<T> register/unregister shape
// Sec2.5 states - re-registering while already registered unregisters
// first.
// ===================================================================
void testLobbyCallbackRegistrationCluster() {
    NullSteamBackend backend;
    SteamBoundary boundary(backend);

    CHECK(!boundary.lobbyCallbackRegistered());
    boundary.registerLobbyDataUpdateCallback();
    CHECK(boundary.lobbyCallbackRegistered());
    std::vector<std::string> expectedFirst = {"SteamAPI_RegisterCallback(id=505)"};
    CHECK(backend.callLog() == expectedFirst);

    // Re-registering while already registered: unregister first, Sec2.5
    // step2.
    boundary.registerLobbyDataUpdateCallback();
    std::vector<std::string> expectedSecond = {
        "SteamAPI_RegisterCallback(id=505)",
        "SteamAPI_UnregisterCallback()",
        "SteamAPI_RegisterCallback(id=505)",
    };
    CHECK(backend.callLog() == expectedSecond);

    boundary.unregisterLobbyDataUpdateCallback();
    CHECK(!boundary.lobbyCallbackRegistered());
    CHECK(backend.callLog().back() == "SteamAPI_UnregisterCallback()");

    // A second unregister while already unregistered is a no-op (no extra
    // backend call).
    size_t sizeBefore = backend.callLog().size();
    boundary.unregisterLobbyDataUpdateCallback();
    CHECK(backend.callLog().size() == sizeBefore);
}

// ===================================================================
// Sec4.3, 0x00b96050 (CONFIRMED): the cloud-storage cluster's explicit
// SteamUser()-null-check-and-soft-fail pattern - returns error code 6 on
// the null branch rather than dereferencing it. 0x00b95eb0 (HIGH): the
// existence-then-size query pair.
// ===================================================================
void testCloudStorageCluster() {
    NullSteamBackend backend;
    SteamBoundary boundary(backend);

    // SteamUser() available (the stub DLL's own default, Sec2.1): the
    // guard passes, success stub returned.
    CHECK(boundary.cloudStorageGuardedOperation() == 0);

    // SteamUser() forced unavailable: the real, deliberate defensive
    // branch - error code 6, no dereference.
    backend.setUserAvailable(false);
    CHECK(boundary.cloudStorageGuardedOperation() == kCloudStorageSteamUserUnavailableError);
    CHECK(kCloudStorageSteamUserUnavailableError == 6);

    // The existence/size query pair.
    backend.setFileQueryResult("save1.sav", true, 1234);
    CloudFileQueryResult r = boundary.cloudStorageFileQuery("save1.sav");
    CHECK(r.exists == true);
    CHECK(r.size == 1234);

    CloudFileQueryResult missing = boundary.cloudStorageFileQuery("no_such_file.sav");
    CHECK(missing.exists == false);
    CHECK(missing.size == 0);
}

// ===================================================================
// Sec3 step5 / Sec5.2: the chat-overlay call shape - SteamFriends()
// vtable+0x50 with the literal string "chat" (0x01165af4). The gating
// condition is OPEN and intentionally not modelled; this tests only the
// confirmed call shape itself.
// ===================================================================
void testChatOverlayCallShape() {
    NullSteamBackend backend;
    SteamBoundary boundary(backend);

    boundary.openChatOverlayWithFriend(76561198000000123ULL);  // arbitrary synthetic Steam64 id
    std::string expected = "ISteamFriends::ActivateGameOverlayToUser(76561198000000123,\"chat\")";
    CHECK(!backend.callLog().empty());
    CHECK(backend.callLog().back() == expected);
    CHECK(std::string(kChatOverlayDialogName) == "chat");
}

// ===================================================================
// Sec2.4: SteamAPI_RestartAppIfNecessary has NO confirmed call site
// anywhere in the census - "a real-SDK binding does not need to reproduce
// any call to it on this build." SteamBoundary never calls it through any
// of its own public surface, across the full lifecycle.
// ===================================================================
void testRestartAppIfNecessaryNeverCalled() {
    NullSteamBackend backend;
    SteamBoundary boundary(backend);

    boundary.init();
    boundary.runCallbacks();
    boundary.requestTypeA(1, 2);
    boundary.requestTypeB(3, 4);
    boundary.drainPendingRequests([](uint32_t, uint32_t) {});
    boundary.registerLobbyDataUpdateCallback();
    boundary.unregisterLobbyDataUpdateCallback();
    boundary.cloudStorageGuardedOperation();
    boundary.cloudStorageFileQuery("x.sav");
    boundary.openChatOverlayWithFriend(1);
    boundary.shutdownRegistry().runAll();

    CHECK(backend.restartAppIfNecessaryCallCount() == 0);
}

}  // namespace

int main() {
    testInitSilentSkipOnFailure();
    testInitGuardByteSubtlety();
    testInitSuccessPathOrderAndContract();
    testCallbackIdConstants();
    testShutdownLifoOrdering();
    testRunCallbacksPerFramePump();
    testPendingRequestQueueCluster();
    testLobbyCallbackRegistrationCluster();
    testCloudStorageCluster();
    testChatOverlayCallShape();
    testRestartAppIfNecessaryNeverCalled();
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_sr3steam_test: all checks passed\n";
    return 0;
}
