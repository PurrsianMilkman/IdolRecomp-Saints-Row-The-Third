// sr3steam/steam_boundary.h implementation. See the header's own doc
// comment for the exact spec-steamworks-boundary.md citations.
#include "sr3steam/steam_boundary.h"

namespace sr3steam {

bool SteamBoundary::init(void* warningHook) {
    if (initGuard_) return true;  // Sec2.2 step1: already succeeded once - no-op

    const bool ok = backend_.steamAPI_Init();  // Sec2.2 step4
    initGuard_ = ok;                           // "store the return value into the guard byte"
    if (!ok) {
        return false;  // Sec2.2 step5: silent skip - nothing else runs, no retry, no dialog, no exit
    }

    // Sec2.2 step7: register the shutdown thunk into the LIFO list -
    // symmetric with the failure path above (only reached on success).
    shutdownRegistry_.push("SteamAPI_Shutdown", [this] { backend_.steamAPI_Shutdown(); });

    // Sec2.2 step8: SteamUser()->GetSteamID(), result stored.
    lastSteamId_ = backend_.getSteamID();

    // Sec2.2 step9 / Sec2.5: register the one standing callback
    // (P2PSessionRequest_t).
    p2pCallback_.registerWith(backend_, &p2pCallbackObj_);

    // Sec2.2 step10: SteamClient()->SetWarningMessageHook(...).
    backend_.setWarningMessageHook(warningHook);

    return true;
}

void SteamBoundary::drainPendingRequests(const DispatchEventFn& dispatchEvent) {
    if (!slotASet_ && !slotBSet_) return;  // Sec3 step2: "if neither slot is set, skip to step 3"

    if (slotBSet_) {  // "if the second slot is set"
        if (dispatchEvent) dispatchEvent(slotB_.first, slotB_.second);
        slotBSet_ = false;
        return;
    }

    // Reached only when slotB is not set; the guard above guarantees
    // slotA must be set in that case.
    if (!oneShotFired_) {  // "otherwise, if a one-shot flag byte is clear"
        backend_.matchmakingSlot0x6c(slotA_.first, slotA_.second);
        oneShotFired_ = true;  // "set the flag so this fires only once"
    }
}

CloudFileQueryResult SteamBoundary::cloudStorageFileQuery(const std::string& fileName) {
    CloudFileQueryResult result;
    result.exists = backend_.remoteStorageFileExists(fileName);
    result.size = backend_.remoteStorageGetFileSize(fileName);
    return result;
}

int32_t SteamBoundary::cloudStorageGuardedOperation() {
    if (!backend_.steamUserAvailable()) return kCloudStorageSteamUserUnavailableError;  // 0x00b96050
    return 0;  // success stub - the operation's own content is OPEN (Sec6 item 12)
}

}  // namespace sr3steam
