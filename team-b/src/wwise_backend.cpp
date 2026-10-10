// sr3audioruntime - NullWwiseBackend: see include/sr3audioruntime/wwise_backend.h
// for the contract this satisfies (spec-audio-runtime.md Sec1/Sec3).

#include "sr3audioruntime/wwise_backend.h"

#include <sstream>

namespace sr3audioruntime {

namespace {
std::string objStr(GameObjectId obj) {
    if (obj == kGlobalObject) return "GLOBAL";
    std::ostringstream ss;
    ss << obj;
    return ss.str();
}
}  // namespace

// --------------------------- Sec3.2 init order ---------------------------

bool NullWwiseBackend::memoryMgrInit(int32_t maxNumPools) {
    log("MemoryMgr::Init(maxNumPools=" + std::to_string(maxNumPools) + ")");
    if (!initStepOk(1)) return false;
    memoryMgrInitialized_ = true;
    return true;
}

bool NullWwiseBackend::streamMgrCreate(uint32_t memorySize) {
    log("StreamMgr::Create(memorySize=" + std::to_string(memorySize) + ")");
    if (!initStepOk(2)) return false;
    streamMgrCreated_ = true;
    return true;
}

int32_t NullWwiseBackend::setupStreamDevice(const WwiseInitParams& params) {
    log("0x0046b850:setupStreamDevice(scheduler=" + std::to_string(params.schedulerType) + ")");
    // Sec1.4 0x0046b850: "Refuses (returns false) unless the device
    // settings' scheduler flag (+0x14) is 2 [deferred, lined-up]."
    if (params.schedulerType != 2) return -1;
    if (refuseNonDeferredScheduler_) return -1;
    if (!initStepOk(3)) return -1;
    // "sets the resolver to the game object only if none is set yet"
    if (!resolverSetToGame_) {
        resolverSetToGame_ = true;
        log("StreamMgr::SetFileLocationResolver(game)");
    }
    deviceId_ = 1;
    deviceCreated_ = true;
    return deviceId_;
}

bool NullWwiseBackend::soundEngineInit(const WwiseInitParams& /*params*/) {
    log("SoundEngine::Init()");
    if (!initStepOk(4)) return false;
    soundEngineInitialized_ = true;
    return true;
}

bool NullWwiseBackend::musicEngineInit() {
    log("MusicEngine::Init()");
    if (!initStepOk(5)) return false;
    musicEngineInitialized_ = true;
    return true;
}

void NullWwiseBackend::registerPlugin(const PluginRegistration& reg) {
    log("RegisterPlugin(type=" + std::to_string(reg.type) + ",company=" + std::to_string(reg.company) +
        ",id=" + std::to_string(reg.id) + ")");
}

void NullWwiseBackend::registerCodec(int32_t company, int32_t codecId) {
    log("RegisterCodec(company=" + std::to_string(company) + ",codec=" + std::to_string(codecId) + ")");
}

void NullWwiseBackend::motionEngineSetPlayerListener(int32_t player, int32_t listener) {
    log("MotionEngine::SetPlayerListener(" + std::to_string(player) + "," + std::to_string(listener) + ")");
}

void NullWwiseBackend::setListenerPipeline(int32_t listener, bool audio, bool motion) {
    log("SetListenerPipeline(" + std::to_string(listener) + "," + (audio ? "audio" : "-") + "," +
        (motion ? "motion" : "-") + ")");
}

void NullWwiseBackend::postCode(int32_t level) {
    log("PostCode/SetLocalOutput(level=" + std::to_string(level) + ") [no-op, Sec3.2 step 9]");
}

int32_t NullWwiseBackend::getSpeakerConfiguration() {
    log("GetSpeakerConfiguration()");
    return kSpeakerConfig5Point1;  // deterministic default for a null backend; AudioRuntime
                                   // maps this to channel count per Sec3.2 step 10's rule.
}

// ------------------------- Sec3.4 shutdown order -------------------------

void NullWwiseBackend::musicEngineTerm() {
    log("MusicEngine::Term()");
    musicEngineInitialized_ = false;
}

bool NullWwiseBackend::soundEngineIsInitialized() {
    return soundEngineInitialized_;
}

void NullWwiseBackend::stopAll(GameObjectId object) {
    log("SoundEngine::StopAll(" + objStr(object) + ")");
}

void NullWwiseBackend::clearPreparedEvents() { log("SoundEngine::ClearPreparedEvents()"); }

void NullWwiseBackend::unregisterAllGameObj() { log("SoundEngine::UnregisterAllGameObj()"); }

void NullWwiseBackend::clearBanks() { log("SoundEngine::ClearBanks()"); }

void NullWwiseBackend::soundEngineTerm() {
    log("SoundEngine::Term()");
    soundEngineInitialized_ = false;
}

void NullWwiseBackend::setFileLocationResolver(bool toGameResolver) {
    log(std::string("StreamMgr::SetFileLocationResolver(") + (toGameResolver ? "game" : "null") + ")");
    resolverSetToGame_ = toGameResolver;
}

void NullWwiseBackend::destroyStreamDevice(int32_t deviceId) {
    log("StreamMgr::DestroyDevice(" + std::to_string(deviceId) + ")");
    deviceCreated_ = false;
    deviceId_ = -1;
}

void NullWwiseBackend::streamMgrDestroyIfExists() {
    if (streamMgrCreated_) {
        log("IAkStreamMgr::Destroy()");
        streamMgrCreated_ = false;
    }
}

void NullWwiseBackend::memoryMgrTerm() {
    log("MemoryMgr::Term()");
    memoryMgrInitialized_ = false;
}

// ----------------------------- Sec1.1 per-voice -----------------------------

bool NullWwiseBackend::registerGameObj(uint64_t gameObjectHandle, const std::string& name) {
    log("RegisterGameObj(" + std::to_string(gameObjectHandle) + ",\"" + name + "\")");
    return forceRegisterResult_;
}

bool NullWwiseBackend::unregisterGameObj(uint64_t gameObjectHandle) {
    log("UnregisterGameObj(" + std::to_string(gameObjectHandle) + ")");
    return true;
}

bool NullWwiseBackend::setGameObjectEnvironmentsValues(GameObjectId object, const std::vector<AuxSend>& sends) {
    log("SetGameObjectEnvironmentsValues(" + objStr(object) + ",count=" + std::to_string(sends.size()) + ")");
    return true;
}

bool NullWwiseBackend::setPosition(GameObjectId object, const SoundPosition& /*position*/, int32_t listenerIndex) {
    log("SetPosition(" + objStr(object) + ",listener=" + std::to_string(listenerIndex) + ")");
    return true;
}

bool NullWwiseBackend::setMultiplePositions(GameObjectId object, const std::vector<SoundPosition>& positions,
                                             int32_t type) {
    log("SetMultiplePositions(" + objStr(object) + ",count=" + std::to_string(positions.size()) +
        ",type=" + std::to_string(type) + ")");
    return true;
}

bool NullWwiseBackend::setObjectObstructionAndOcclusion(GameObjectId object, int32_t listenerIndex,
                                                          float obstruction, float occlusion) {
    log("SetObjectObstructionAndOcclusion(" + objStr(object) + "," + std::to_string(listenerIndex) + "," +
        std::to_string(obstruction) + "," + std::to_string(occlusion) + ")");
    return true;
}

bool NullWwiseBackend::setRTPCValue(uint32_t rtpcId, float value, GameObjectId object) {
    std::ostringstream ss;
    ss << "SetRTPCValue(0x" << std::hex << rtpcId << std::dec << "," << value << "," << objStr(object) << ")";
    log(ss.str());
    return forceSetRtpcResult_;
}

bool NullWwiseBackend::setSwitch(uint32_t switchGroup, uint32_t switchState, GameObjectId object) {
    log("SetSwitch(" + std::to_string(switchGroup) + "," + std::to_string(switchState) + "," + objStr(object) + ")");
    // Sec10.2.2: CAkRTPCMgr::SetSwitchInternal is pure bookkeeping (no
    // curve, no property application) and always stores+notifies, even
    // when unchanged - never fails from this call's own closure.
    switchTable_.setSwitch(object, switchGroup, switchState);
    return true;
}

// ----------------------------- Sec1.2 play record -----------------------------

uint32_t NullWwiseBackend::postEvent(uint32_t eventId, GameObjectId object, uint32_t flags, PostEventCallback callback,
                                      uint32_t cookie, const std::vector<ExternalSourceInfo>& externalSources) {
    log("PostEvent(event=" + std::to_string(eventId) + "," + objStr(object) + ",flags=" + std::to_string(flags) +
        ",cookie=" + std::to_string(cookie) + ",extSrc=" + std::to_string(externalSources.size()) + ")");
    if (failNextPostEvent_) {
        failNextPostEvent_ = false;
        return 0;
    }
    uint32_t playingId = nextPlayingId_++;
    pendingEvents_[playingId] = PendingEvent{std::move(callback), cookie};
    return playingId;
}

bool NullWwiseBackend::stopPlayingID(uint32_t playingId, int32_t fadeMs, int32_t curve) {
    log("StopPlayingID(" + std::to_string(playingId) + "," + std::to_string(fadeMs) + "," + std::to_string(curve) +
        ")");
    return true;
}

bool NullWwiseBackend::executeActionOnEvent(uint32_t eventId, EventAction action, GameObjectId object, int32_t fadeMs,
                                             int32_t curve) {
    log("ExecuteActionOnEvent(event=" + std::to_string(eventId) + ",action=" +
        std::to_string(static_cast<int32_t>(action)) + "," + objStr(object) + "," + std::to_string(fadeMs) + "," +
        std::to_string(curve) + ")");
    return true;
}

bool NullWwiseBackend::seekOnEvent(uint32_t eventId, GameObjectId object, int32_t positionMs,
                                    bool seekToNearestMarker) {
    log("SeekOnEvent(event=" + std::to_string(eventId) + "," + objStr(object) + "," + std::to_string(positionMs) +
        "," + (seekToNearestMarker ? "nearest" : "exact") + ")");
    return forceSeekResult_;
}

// ----------------------------- Sec1.3 global -----------------------------

bool NullWwiseBackend::setListenerPosition(const ListenerPosition& /*position*/, int32_t listenerIndex) {
    log("SetListenerPosition(listener=" + std::to_string(listenerIndex) + ")");
    return true;
}

void NullWwiseBackend::renderAudio() { log("RenderAudio()"); }

bool NullWwiseBackend::setState(uint32_t stateGroup, uint32_t state) {
    log("SetState(" + std::to_string(stateGroup) + "," + std::to_string(state) + ")");
    // Sec10.2.3: the id-only layer is all the public SetState(group,state)
    // call alone can drive - the 4-property ramp-or-snap apply
    // (StateGroupEngine::applyProperties) needs the AUTHORED new value-set
    // from bank/HIRC data this backend has no access to, and is never
    // invented here (see switch_state_internal.h's own header comment).
    stateEngine_.setState(stateGroup, state);
    return true;
}

uint32_t NullWwiseBackend::getIDFromString(const std::string& name) {
    // Public FNV-1 (32-bit), offset basis 0x811c9dc5, prime 0x01000193 - a
    // generic, publicly documented hash algorithm; Sec1.3 names it ("FNV-1",
    // "Already specified") but gives no further detail to add here, and
    // null/empty/"none" never reach this method (AudioRuntime's own
    // short-circuit, Sec1.3 row 0x0046fd00).
    uint32_t hash = 0x811c9dc5u;
    for (unsigned char c : name) {
        hash *= 0x01000193u;
        hash ^= c;
    }
    log("GetIDFromString(\"" + name + "\") -> 0x" + std::to_string(hash));
    return hash;
}

// ----------------------------- Sec1.4 banks -----------------------------

bool NullWwiseBackend::loadBank(uint32_t bankId, BankCallback callback, uint32_t cookie, int32_t memoryPool) {
    log("LoadBank(bank=" + std::to_string(bankId) + ",cookie=" + std::to_string(cookie) +
        ",pool=" + std::to_string(memoryPool) + ")");
    pendingBankLoads_[bankId] = PendingBank{std::move(callback), cookie};
    return true;
}

bool NullWwiseBackend::unloadBank(uint32_t bankId, BankCallback callback, uint32_t cookie) {
    log("UnloadBank(bank=" + std::to_string(bankId) + ",cookie=" + std::to_string(cookie) + ")");
    pendingBankUnloads_[bankId] = PendingBank{std::move(callback), cookie};
    return true;
}

bool NullWwiseBackend::setBankLoadIOSettings(double throughput, int32_t priority) {
    log("SetBankLoadIOSettings(" + std::to_string(throughput) + "," + std::to_string(priority) + ")");
    return true;
}

int32_t NullWwiseBackend::createPool(size_t size) {
    log("MemoryMgr::CreatePool(size=" + std::to_string(size) + ")");
    return nextPoolId_++;
}

bool NullWwiseBackend::setPoolName(int32_t poolId, const std::string& name) {
    log("SetPoolName(" + std::to_string(poolId) + ",\"" + name + "\")");
    return true;
}

bool NullWwiseBackend::stopSourcePlugin(int32_t pluginId, int32_t company, uint32_t playingId) {
    log("StopSourcePlugin(" + std::to_string(pluginId) + "," + std::to_string(company) + "," +
        std::to_string(playingId) + ")");
    return true;
}

// ----------------------------- harness hooks -----------------------------

bool NullWwiseBackend::fireEventCallback(uint32_t playingId, WwiseCallbackType type, const std::string& markerLabel) {
    auto it = pendingEvents_.find(playingId);
    if (it == pendingEvents_.end() || !it->second.callback) return false;
    it->second.callback(type, it->second.cookie, markerLabel);
    return true;
}

bool NullWwiseBackend::completeBankLoad(uint32_t bankId, int32_t wwiseResult) {
    auto it = pendingBankLoads_.find(bankId);
    if (it == pendingBankLoads_.end() || !it->second.callback) return false;
    BankCallback cb = it->second.callback;
    uint32_t cookie = it->second.cookie;
    pendingBankLoads_.erase(it);
    cb(bankId, wwiseResult, cookie);
    return true;
}

bool NullWwiseBackend::completeBankUnload(uint32_t bankId, int32_t wwiseResult) {
    auto it = pendingBankUnloads_.find(bankId);
    if (it == pendingBankUnloads_.end() || !it->second.callback) return false;
    BankCallback cb = it->second.callback;
    uint32_t cookie = it->second.cookie;
    pendingBankUnloads_.erase(it);
    cb(bankId, wwiseResult, cookie);
    return true;
}

}  // namespace sr3audioruntime
