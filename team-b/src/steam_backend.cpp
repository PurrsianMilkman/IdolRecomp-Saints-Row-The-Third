// sr3steam/steam_backend.h implementation - NullSteamBackend. See the
// header's own doc comment for the contract this satisfies
// (spec-steamworks-boundary.md Sec1/Sec2/Sec3/Sec4).
#include "sr3steam/steam_backend.h"

namespace sr3steam {

bool NullSteamBackend::steamAPI_Init() {
    log("SteamAPI_Init()");
    initCalled_ = true;
    return !forceInitFailure_;
}

void NullSteamBackend::steamAPI_RunCallbacks() {
    log("SteamAPI_RunCallbacks()");
    ++runCallbacksCount_;
}

void NullSteamBackend::steamAPI_Shutdown() {
    log("SteamAPI_Shutdown()");
    shutdownCalled_ = true;
}

bool NullSteamBackend::steamAPI_RestartAppIfNecessary(uint32_t appId) {
    log("SteamAPI_RestartAppIfNecessary(" + std::to_string(appId) + ")");
    ++restartAppIfNecessaryCallCount_;
    return false;  // stub DLL body: xor al,al ; ret (Sec2.1, CONFIRMED)
}

void NullSteamBackend::steamAPI_RegisterCallback(void* callbackObj, int32_t callbackId) {
    (void)callbackObj;
    log("SteamAPI_RegisterCallback(id=" + std::to_string(callbackId) + ")");
}

void NullSteamBackend::steamAPI_UnregisterCallback(void* callbackObj) {
    (void)callbackObj;
    log("SteamAPI_UnregisterCallback()");
}

void NullSteamBackend::steamAPI_RegisterCallResult(void* callbackObj, uint64_t apiCall) {
    (void)callbackObj;
    log("SteamAPI_RegisterCallResult(apiCall=" + std::to_string(apiCall) + ")");
}

void NullSteamBackend::steamAPI_UnregisterCallResult(void* callbackObj, uint64_t apiCall) {
    (void)callbackObj;
    log("SteamAPI_UnregisterCallResult(apiCall=" + std::to_string(apiCall) + ")");
}

uint64_t NullSteamBackend::getSteamID() {
    log("ISteamUser::GetSteamID()");
    return steamId_;
}

void NullSteamBackend::setWarningMessageHook(void* hook) {
    log("ISteamClient::SetWarningMessageHook()");
    lastWarningHook_ = hook;
}

void NullSteamBackend::matchmakingSlot0x6c(uint32_t a, uint32_t b) {
    log("ISteamMatchmaking::+0x6c(" + std::to_string(a) + "," + std::to_string(b) + ")");
}

void NullSteamBackend::activateGameOverlayToUser(uint64_t steamId, const std::string& dialogName) {
    log("ISteamFriends::ActivateGameOverlayToUser(" + std::to_string(steamId) + ",\"" + dialogName + "\")");
}

bool NullSteamBackend::remoteStorageFileExists(const std::string& fileName) {
    log("ISteamRemoteStorage::FileExists(\"" + fileName + "\")");
    for (const auto& kv : fileQueryResults_) {
        if (kv.first == fileName) return kv.second.exists;
    }
    return false;
}

int32_t NullSteamBackend::remoteStorageGetFileSize(const std::string& fileName) {
    log("ISteamRemoteStorage::GetFileSize(\"" + fileName + "\")");
    for (const auto& kv : fileQueryResults_) {
        if (kv.first == fileName) return kv.second.size;
    }
    return 0;
}

void NullSteamBackend::setFileQueryResult(const std::string& fileName, bool exists, int32_t size) {
    for (auto& kv : fileQueryResults_) {
        if (kv.first == fileName) {
            kv.second = {exists, size};
            return;
        }
    }
    fileQueryResults_.push_back({fileName, {exists, size}});
}

}  // namespace sr3steam
