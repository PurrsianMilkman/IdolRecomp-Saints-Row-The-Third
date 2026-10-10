#pragma once

// sr3steam/steam_backend.h - the Steamworks call-boundary CONTRACT
// (spec-steamworks-boundary.md, "Saints Row: The Third - Steamworks Call
// Boundary (Game Side Only)"), the GAME's own side of the 17-import/
// 104-call-site boundary into Steam (Sec1).
//
// SteamBackend is the boundary interface itself: every method is one of the
// 17 imported Steamworks entry points (Sec1's own census table) or one of
// the handful of CONFIRMED/HIGH-confidence virtual-method calls the init
// (Sec2.2), per-frame (Sec3), or on-demand-cluster (Sec4) contracts make
// through an interface accessor's vtable. SteamBoundary (steam_boundary.h)
// is the GAME side; it calls these methods in exactly the order and shape
// Sec2/Sec3/Sec4 state. A real backend would implement this interface by
// linking the actual Steamworks SDK - explicitly a LATER task, once the SDK
// is available (Sec0's own scope limit: "Steamworks itself is not
// reverse-engineered here. ... Team B will link the real Steamworks SDK
// against this contract"). NullSteamBackend below implements it with no
// SDK at all - a deterministic, in-process stand-in whose DEFAULT behaviour
// matches the shipped GOG build's own stub steam_api.dll CONFIRMED bodies
// (Sec2.1): SteamAPI_Init always succeeds, every interface accessor is
// always available (non-null). Every behaviour below can individually be
// forced to the OTHER branch, so the real failure paths - dead on the stub
// DLL itself, but real code a genuine SDK binding must satisfy - are
// exercisable. Getting the Init-failure silent-skip path (Sec2.2 step5)
// right is this project's single most important Steam-boundary behaviour,
// since it is the path this project's own host actually runs on every
// launch (no real Steam client present in this clean-room dev/test
// environment).
//
// The 17 imports (Sec1, IAT slots 0x0101c544-0x0101c587), named exactly as
// read from steam_api.dll's own PE export table - the publisher's own
// public API surface, quoted as data per Sec0/Sec7 (the same convention
// this project already applies to Wwise's and Bink's public API names):
//   SteamAPI_Init, SteamAPI_RunCallbacks, SteamAPI_Shutdown,
//   SteamAPI_RestartAppIfNecessary, SteamAPI_RegisterCallback,
//   SteamAPI_UnregisterCallback, SteamAPI_RegisterCallResult,
//   SteamAPI_UnregisterCallResult, SteamClient, SteamUser, SteamFriends,
//   SteamMatchmaking, SteamRemoteStorage, SteamApps, SteamUtils,
//   SteamUserStats, SteamNetworking.
//
// Interface accessors (SteamUser() et al.) are modelled as a simple
// "available" boolean rather than a real pointer/handle type: nothing past
// an accessor's own non-null/null-ness is in this spec's scope (Sec2.1:
// "every accessor returns non-null... but they are real code"; Sec4.3's
// null-check-and-soft-fail cluster, 0x00b96050, tests exactly this
// boolean - "the one place in the whole census where the game's own code
// is written expecting the interface accessor to sometimes come back
// unavailable at the individual-call level"). The handful of CONFIRMED/HIGH
// virtual calls made THROUGH a resolved interface (Sec2.2 steps 8/10, Sec3
// steps 2/5, Sec4.3) are flat methods below, each cited to its own vtable
// slot and confidence level; no other part of any Steam interface's layout
// is modelled.
//
// OUT OF SCOPE here (per the task brief and Sec0's own scope limit): this
// is NOT a Steamworks SDK integration. No real SDK header is linked, no
// Steam App ID, achievement name, or other Steam-specific identifier is
// invented anywhere below - every constant that follows is either read
// directly off this spec's own CONFIRMED text or is a HIGH-confidence
// numeric match to a publicly published Valve header enum value, cited
// exactly as the spec cites it.

#include <cstdint>
#include <string>
#include <vector>

namespace sr3steam {

// Sec2.5 / Sec4.1: the two CONFIRMED callback ids this spec traces, each a
// numeric match to Valve's own publicly published steamclientpublic.h enum
// values (HIGH CONFIDENCE, naming only - same convention as this project's
// existing Wwise/Bink public-name citations).
constexpr int32_t kP2PSessionRequestCallbackId = 1202;  // 0x4b2, Sec2.5 (k_iSteamNetworkingCallbacks(1200)+2)
constexpr int32_t kLobbyDataUpdateCallbackId = 505;     // 0x1f9, Sec4.1 (k_iSteamMatchmakingCallbacks(500)+5)

// Sec4.3, 0x00b96050 (CONFIRMED): the error code the cluster returns on its
// explicit SteamUser()-null-check branch, instead of dereferencing it.
constexpr int32_t kCloudStorageSteamUserUnavailableError = 6;

// Sec3 step5 / Sec7: the literal string read directly from the executable's
// own static data (0x01165af4), the dialog name passed to the chat-overlay
// call.
constexpr const char* kChatOverlayDialogName = "chat";

// The boundary interface: every method below is cited to its own spec
// section in steam_boundary.h, which is the only place these are called.
class SteamBackend {
public:
    virtual ~SteamBackend() = default;

    // -----------------------------------------------------------------
    // Sec1: the 17 imports, as flat calls matching the publisher's own
    // export names (quoted as data, Sec0/Sec7).
    // -----------------------------------------------------------------
    virtual bool steamAPI_Init() = 0;
    virtual void steamAPI_RunCallbacks() = 0;
    // Never called directly anywhere in this contract (Sec2.3) - only
    // reachable through the LIFO shutdown registry's walk (steam_boundary.h
    // ShutdownRegistry).
    virtual void steamAPI_Shutdown() = 0;
    // No confirmed call site anywhere in the census (Sec2.4) - present only
    // because it is one of the 17 imports. SteamBoundary never calls this;
    // see synthetic_sr3steam_test.cpp's own test asserting exactly that.
    virtual bool steamAPI_RestartAppIfNecessary(uint32_t appId) = 0;
    // Sec2.5 steps 2/4: register/unregister the one standing callback -
    // also reused by Sec4.1's lobby callback (identical CCallback<T> shape).
    virtual void steamAPI_RegisterCallback(void* callbackObj, int32_t callbackId) = 0;
    virtual void steamAPI_UnregisterCallback(void* callbackObj) = 0;
    // Sec1's own census notes these two (RegisterCallResult 1 site vs.
    // UnregisterCallResult 7 sites, OPEN item 1) without resolving their
    // exact argument shape; present for completeness as 2 of the 17
    // imports, not wired into any SteamBoundary cluster below.
    virtual void steamAPI_RegisterCallResult(void* callbackObj, uint64_t apiCall) = 0;
    virtual void steamAPI_UnregisterCallResult(void* callbackObj, uint64_t apiCall) = 0;

    virtual bool steamClientAvailable() = 0;         // SteamClient()
    virtual bool steamUserAvailable() = 0;           // SteamUser()
    virtual bool steamFriendsAvailable() = 0;        // SteamFriends()
    virtual bool steamMatchmakingAvailable() = 0;    // SteamMatchmaking()
    virtual bool steamRemoteStorageAvailable() = 0;  // SteamRemoteStorage()
    virtual bool steamAppsAvailable() = 0;           // SteamApps()
    virtual bool steamUtilsAvailable() = 0;          // SteamUtils()
    virtual bool steamUserStatsAvailable() = 0;      // SteamUserStats()
    virtual bool steamNetworkingAvailable() = 0;     // SteamNetworking()

    // -----------------------------------------------------------------
    // CONFIRMED/HIGH virtual calls made THROUGH a resolved interface, each
    // cited to its own vtable slot (Sec2.2, Sec3, Sec4.3). The stub DLL's
    // own accessors are always available (Sec2.1) so these are reachable
    // unconditionally there; a real SDK binding is the reason
    // steamUserAvailable() above exists as a separate, checkable call.
    // -----------------------------------------------------------------
    // ISteamUser vtable +0x8 (HIGH CONFIDENCE GetSteamID, Sec2.2 step8).
    virtual uint64_t getSteamID() = 0;
    // ISteamClient vtable +0x54 (HIGH CONFIDENCE SetWarningMessageHook,
    // Sec2.2 step10). `hook` is an opaque function-pointer value - its real
    // signature is the public SDK header's own and is not reproduced here;
    // "a real binding supplies its own equivalent hook" (Sec2.2's own
    // words).
    virtual void setWarningMessageHook(void* hook) = 0;
    // ISteamMatchmaking vtable +0x6c (Sec3 step2): fired at most once per
    // process, gated by the per-frame tick's own one-shot flag.
    virtual void matchmakingSlot0x6c(uint32_t a, uint32_t b) = 0;
    // ISteamFriends vtable +0x50 (HIGH CONFIDENCE ActivateGameOverlayToUser,
    // Sec3 step5, cross-referenced to the already-specced Lua binding at
    // Sec5.2 which uses the identical slot).
    virtual void activateGameOverlayToUser(uint64_t steamId, const std::string& dialogName) = 0;
    // ISteamRemoteStorage vtable +0xc then +0x4 (Sec4.3, 0x00b95eb0, HIGH:
    // an existence query then a size/info query - not confirmed to exact
    // method names; FileExists/GetFileSize are the public SDK's own shape
    // for this pair, cited for naming only).
    virtual bool remoteStorageFileExists(const std::string& fileName) = 0;
    virtual int32_t remoteStorageGetFileSize(const std::string& fileName) = 0;

    // Ordered log of every call made, for asserting Sec2/Sec3/Sec4 call-
    // order fidelity against a null backend.
    virtual const std::vector<std::string>& callLog() const = 0;
};

// -----------------------------------------------------------------------
// NullSteamBackend: a deterministic, in-process, no-SDK implementation of
// the contract above, matching the shipped stub DLL's own CONFIRMED default
// bodies (Sec2.1) - Init always succeeds, every accessor is always
// available, RestartAppIfNecessary always reports false (`xor al,al ; ret`,
// CONFIRMED) - with every behaviour individually force-overridable so the
// real failure branches (dead on the stub, real on a genuine SDK) are
// exercisable in tests.
// -----------------------------------------------------------------------
class NullSteamBackend : public SteamBackend {
public:
    NullSteamBackend() = default;

    bool steamAPI_Init() override;
    void steamAPI_RunCallbacks() override;
    void steamAPI_Shutdown() override;
    bool steamAPI_RestartAppIfNecessary(uint32_t appId) override;
    void steamAPI_RegisterCallback(void* callbackObj, int32_t callbackId) override;
    void steamAPI_UnregisterCallback(void* callbackObj) override;
    void steamAPI_RegisterCallResult(void* callbackObj, uint64_t apiCall) override;
    void steamAPI_UnregisterCallResult(void* callbackObj, uint64_t apiCall) override;

    bool steamClientAvailable() override { log("SteamClient()"); return clientAvailable_; }
    bool steamUserAvailable() override { log("SteamUser()"); return userAvailable_; }
    bool steamFriendsAvailable() override { log("SteamFriends()"); return friendsAvailable_; }
    bool steamMatchmakingAvailable() override { log("SteamMatchmaking()"); return matchmakingAvailable_; }
    bool steamRemoteStorageAvailable() override { log("SteamRemoteStorage()"); return remoteStorageAvailable_; }
    bool steamAppsAvailable() override { log("SteamApps()"); return appsAvailable_; }
    bool steamUtilsAvailable() override { log("SteamUtils()"); return utilsAvailable_; }
    bool steamUserStatsAvailable() override { log("SteamUserStats()"); return userStatsAvailable_; }
    bool steamNetworkingAvailable() override { log("SteamNetworking()"); return networkingAvailable_; }

    uint64_t getSteamID() override;
    void setWarningMessageHook(void* hook) override;
    void matchmakingSlot0x6c(uint32_t a, uint32_t b) override;
    void activateGameOverlayToUser(uint64_t steamId, const std::string& dialogName) override;
    bool remoteStorageFileExists(const std::string& fileName) override;
    int32_t remoteStorageGetFileSize(const std::string& fileName) override;

    const std::vector<std::string>& callLog() const override { return log_; }

    // --- Test-only configuration (never consulted by real behaviour) ---
    // Models SteamAPI_Init failing - the entire reason Sec2.2's silent-skip
    // path exists; dead on the stub DLL itself (Sec2.1) but the single most
    // important behaviour for a real-SDK binding to get right.
    void setForceInitFailure(bool fail) { forceInitFailure_ = fail; }
    void setClientAvailable(bool avail) { clientAvailable_ = avail; }
    void setUserAvailable(bool avail) { userAvailable_ = avail; }
    void setFriendsAvailable(bool avail) { friendsAvailable_ = avail; }
    void setMatchmakingAvailable(bool avail) { matchmakingAvailable_ = avail; }
    void setRemoteStorageAvailable(bool avail) { remoteStorageAvailable_ = avail; }
    void setAppsAvailable(bool avail) { appsAvailable_ = avail; }
    void setUtilsAvailable(bool avail) { utilsAvailable_ = avail; }
    void setUserStatsAvailable(bool avail) { userStatsAvailable_ = avail; }
    void setNetworkingAvailable(bool avail) { networkingAvailable_ = avail; }
    void setSteamIDValue(uint64_t id) { steamId_ = id; }
    void setFileQueryResult(const std::string& fileName, bool exists, int32_t size);

    bool initCalled() const { return initCalled_; }
    bool shutdownCalled() const { return shutdownCalled_; }
    int32_t runCallbacksCount() const { return runCallbacksCount_; }
    int32_t restartAppIfNecessaryCallCount() const { return restartAppIfNecessaryCallCount_; }
    void* lastWarningHook() const { return lastWarningHook_; }

private:
    void log(const std::string& entry) { log_.push_back(entry); }

    std::vector<std::string> log_;

    bool forceInitFailure_ = false;
    bool initCalled_ = false;
    bool shutdownCalled_ = false;
    int32_t runCallbacksCount_ = 0;
    int32_t restartAppIfNecessaryCallCount_ = 0;

    bool clientAvailable_ = true;
    bool userAvailable_ = true;
    bool friendsAvailable_ = true;
    bool matchmakingAvailable_ = true;
    bool remoteStorageAvailable_ = true;
    bool appsAvailable_ = true;
    bool utilsAvailable_ = true;
    bool userStatsAvailable_ = true;
    bool networkingAvailable_ = true;

    uint64_t steamId_ = 0;
    void* lastWarningHook_ = nullptr;

    struct FileQueryResult {
        bool exists = false;
        int32_t size = 0;
    };
    std::vector<std::pair<std::string, FileQueryResult>> fileQueryResults_;
};

}  // namespace sr3steam
