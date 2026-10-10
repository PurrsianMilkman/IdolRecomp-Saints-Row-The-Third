#pragma once

// sr3audioruntime - the Wwise adapter contract (spec-audio-runtime.md,
// title: "Audio Runtime: the Wwise Adapter Contract").
//
// WwiseBackend is the boundary interface itself: every method is one of the
// 25 calling-function crossings of Sec1, or one of the ordered init/shutdown
// sub-steps of Sec3.1/Sec3.2/Sec3.4. AudioRuntime (audio_runtime.h) is the
// GAME side; it calls these methods with exactly the arguments Sec1's tables
// give, in exactly the order Sec2.4/Sec3's prose gives, and interprets the
// return values per each row's "Guard -> return" column. A real Wwise
// adapter would implement this interface by calling into the actual
// Audiokinetic SDK; NullWwiseBackend below implements it with no engine at
// all - a deterministic, in-process simulation - which is all this project
// wants or needs (see the task's backend note: no real Wwise SDK, no real
// decoding/playback, no real threading).
//
// Every method name and argument list is the PUBLIC Wwise SDK call named in
// Sec1/Sec3 (the binary's own PE export-table names, third-party and out of
// scope in themselves per Sec0). Nothing about Wwise's own internals is
// modelled; NullWwiseBackend's bodies just log the call and return a
// deterministic, configurable result.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "sr3audioruntime/switch_state_internal.h"
#include "sr3audioruntime/types.h"

namespace sr3audioruntime {

// Settings threaded through the Wwise init sequence (Sec3.2). Only the
// fields the spec says the game sets are modelled; everything else is "the
// SDK default", which a null backend has no need to represent at all.
struct WwiseInitParams {
    int32_t maxNumPools = 0;             // MemoryMgr settings +0x00 (Sec3.2 step1)
    uint32_t streamMgrMemorySize = 0;    // StreamMgr settings +0x00 (step2)
    uint32_t ioMemorySize = 0;           // device settings +0x04, from config 0x03172864 (step3)
    uint32_t granularity = 0x6800;       // device settings +0x10 (step3, CONFIRMED)
    int32_t schedulerType = 2;           // device settings +0x14 (step3, CONFIRMED: deferred lined-up)
    uint32_t ioThreadStack = 0x10000;    // device settings +0x20 (step3, CONFIRMED)
    int32_t maxConcurrentIo = 0x12;      // device settings +0x28 (step3, CONFIRMED)
    uint32_t defaultPoolSize = 0;        // init settings +0x0c, from config 0x03172868 (step4)
    double poolRatioThreshold = 0.95;    // init settings +0x10 (step4, CONFIRMED)
    uint64_t windowHandle = 0;           // platform settings +0x00, from config 0x03172970 (step4)
    int32_t lowerEngineThreadPriority = 2;  // platform +0x04 (step4, CONFIRMED)
    uint32_t lowerEngineStack = 0x8000;      // platform +0x0c (step4, CONFIRMED)
    uint32_t bankManagerThreadStack = 0x8000;  // platform +0x18 (step4, CONFIRMED)
    uint32_t lowerEnginePoolSize = 0;        // platform +0x1c, from config 0x0317286c (step4)
    double lowerEnginePoolRatioThreshold = 0.85;  // platform +0x20 (step4, CONFIRMED)
    uint16_t refillsPerVoice = 5;             // platform +0x24 (step4, CONFIRMED)
};

// Result of GetSpeakerConfiguration (Sec3.2 step10): the game only tests for
// the 5.1 value 0x3f, else treats it as stereo.
constexpr int32_t kSpeakerConfig5Point1 = 0x3f;

// One plugin/codec registration row from Sec3.2's table, kept only for the
// init-order call log; the factory pointers are the binary's own addresses
// and carry no behaviour here.
struct PluginRegistration {
    int32_t type = 0;
    int32_t company = 0;
    int32_t id = 0;
};

class WwiseBackend {
public:
    virtual ~WwiseBackend() = default;

    // -----------------------------------------------------------------
    // Sec3.2: Wwise init, in CONFIRMED order. AudioRuntime::init() calls
    // these in exactly this order and stops at the first failure (Sec3.2:
    // "Returns 1 on success, 0 on the first failure, with no rollback").
    // -----------------------------------------------------------------
    virtual bool memoryMgrInit(int32_t maxNumPools) = 0;                      // step 1
    virtual bool streamMgrCreate(uint32_t memorySize) = 0;                    // step 2
    // step 3 (0x0046b850, also Sec1.4's own row): sets the file-location
    // resolver only if none is set yet, then creates the stream device.
    // Returns the device id, or -1 on failure (including when
    // params.schedulerType != 2, which the real 0x0046b850 refuses).
    virtual int32_t setupStreamDevice(const WwiseInitParams& params) = 0;
    virtual bool soundEngineInit(const WwiseInitParams& params) = 0;          // step 4
    virtual bool musicEngineInit() = 0;                                       // step 5
    virtual void registerPlugin(const PluginRegistration& reg) = 0;          // step 6 (x18)
    virtual void registerCodec(int32_t company, int32_t codecId) = 0;        // step 6 (Vorbis, codec 4)
    virtual void motionEngineSetPlayerListener(int32_t player, int32_t listener) = 0;  // step 7
    virtual void setListenerPipeline(int32_t listener, bool audio, bool motion) = 0;   // step 8
    virtual void postCode(int32_t level) = 0;  // step 9: the folded SetLocalOutput no-op (Sec3.2)
    virtual int32_t getSpeakerConfiguration() = 0;  // step 10

    // -----------------------------------------------------------------
    // Sec3.4: Wwise shutdown (0x004701b0), in CONFIRMED order - the exact
    // reverse of init.
    // -----------------------------------------------------------------
    virtual void musicEngineTerm() = 0;
    virtual bool soundEngineIsInitialized() = 0;
    virtual void stopAll(GameObjectId object) = 0;         // object == -1: all objects
    virtual void clearPreparedEvents() = 0;
    virtual void unregisterAllGameObj() = 0;
    virtual void clearBanks() = 0;
    virtual void soundEngineTerm() = 0;
    virtual void setFileLocationResolver(bool toGameResolver) = 0;  // false = null
    virtual void destroyStreamDevice(int32_t deviceId) = 0;
    virtual void streamMgrDestroyIfExists() = 0;
    virtual void memoryMgrTerm() = 0;

    // -----------------------------------------------------------------
    // Sec1.1: per-voice parameter and position path.
    // -----------------------------------------------------------------
    // 0x00470610. name is the voice handle printed with "%u" (Sec1.1).
    virtual bool registerGameObj(uint64_t gameObjectHandle, const std::string& name) = 0;
    // 0x0045cc9f/0x0045cca5. Returns false only to model "Wwise result != 1"
    // (a guard failure is decided by AudioRuntime before this is called).
    virtual bool unregisterGameObj(uint64_t gameObjectHandle) = 0;
    // 0x0045ce56 (clear-all-sends call, count 0) and 0x00470a7e/.../b30 (the
    // 4 aux-send sites, up to 3 pairs) share one Wwise entry point.
    virtual bool setGameObjectEnvironmentsValues(GameObjectId object, const std::vector<AuxSend>& sends) = 0;
    // 0x004702ef.
    virtual bool setPosition(GameObjectId object, const SoundPosition& position, int32_t listenerIndex) = 0;
    // 0x00470432. type 2 = MultiDirections (Sec1.1; the only type this
    // adapter ever sends).
    virtual bool setMultiplePositions(GameObjectId object, const std::vector<SoundPosition>& positions, int32_t type) = 0;
    // 0x004704de.
    virtual bool setObjectObstructionAndOcclusion(GameObjectId object, int32_t listenerIndex, float obstruction, float occlusion) = 0;
    // 0x00470997 (per-object and global share one entry point; object == -1
    // is the global form).
    virtual bool setRTPCValue(uint32_t rtpcId, float value, GameObjectId object) = 0;
    // 0x0045d3a0.
    virtual bool setSwitch(uint32_t switchGroup, uint32_t switchState, GameObjectId object) = 0;

    // -----------------------------------------------------------------
    // Sec1.2: the play-record state machine's Wwise-facing operations.
    // -----------------------------------------------------------------
    // 0x0047074e / 0x004705c4. Returns the playing id, or 0 on failure
    // (Sec1.2: "Zero playing id -> status 8 ... and -0x137"). `callback` is
    // remembered against the returned playing id until fireCallback() (or
    // the record is unregistered); `cookie` is handed back unchanged.
    virtual uint32_t postEvent(uint32_t eventId, GameObjectId object, uint32_t flags, PostEventCallback callback,
                                uint32_t cookie, const std::vector<ExternalSourceInfo>& externalSources) = 0;
    // 0x004707ff. The Wwise result is ignored by the caller (Sec1.2); this
    // still returns a bool so a backend CAN model a hard failure if a test
    // wants one, but AudioRuntime does not consult it to decide status.
    virtual bool stopPlayingID(uint32_t playingId, int32_t fadeMs, int32_t curve) = 0;
    // 0x0045d003 (pause) / 0x0045d05d (resume), same entry point,
    // EventAction selects which.
    virtual bool executeActionOnEvent(uint32_t eventId, EventAction action, GameObjectId object, int32_t fadeMs, int32_t curve) = 0;
    // 0x004707bc (millisecond overload).
    virtual bool seekOnEvent(uint32_t eventId, GameObjectId object, int32_t positionMs, bool seekToNearestMarker) = 0;

    // -----------------------------------------------------------------
    // Sec1.3: global (non-object) calls.
    // -----------------------------------------------------------------
    // 0x0045f733.
    virtual bool setListenerPosition(const ListenerPosition& position, int32_t listenerIndex) = 0;
    // 0x0045f945 (tail jump; no return value in the real ABI).
    virtual void renderAudio() = 0;
    // 0x00462999.
    virtual bool setState(uint32_t stateGroup, uint32_t state) = 0;
    // 0x0046fd61 (wide overload). Null/empty/"none" never reach here -
    // AudioRuntime handles that short-circuit itself (Sec1.3).
    virtual uint32_t getIDFromString(const std::string& name) = 0;

    // -----------------------------------------------------------------
    // Sec1.4: banks, streaming, audio input.
    // -----------------------------------------------------------------
    // 0x00465e34 (async id overload). `callback`/`cookie` are remembered
    // against `bankId`; completion is delivered through
    // completeBankLoad()/completeBankUnload() (there is no real bank
    // thread here).
    virtual bool loadBank(uint32_t bankId, BankCallback callback, uint32_t cookie, int32_t memoryPool) = 0;
    // 0x00466072 / 0x0046615c (two call sites, same entry point).
    virtual bool unloadBank(uint32_t bankId, BankCallback callback, uint32_t cookie) = 0;
    // 0x0046687b.
    virtual bool setBankLoadIOSettings(double throughput, int32_t priority) = 0;
    // 0x00470110 / 0x00463420. Returns the pool id, or -1 on failure.
    virtual int32_t createPool(size_t size) = 0;
    // 0x00470160.
    virtual bool setPoolName(int32_t poolId, const std::string& name) = 0;
    // 0x0046f910.
    virtual bool stopSourcePlugin(int32_t pluginId, int32_t company, uint32_t playingId) = 0;

    // -----------------------------------------------------------------
    // Test/harness hooks for the asynchronous crossings. These are NOT
    // part of the Wwise public API; they are how a test simulates
    // "whichever Wwise thread delivers callbacks" (Sec4.2) firing a
    // PostEvent callback or a bank completion. A real adapter would not
    // need these at all (the real SDK calls back on its own thread).
    // -----------------------------------------------------------------
    virtual bool fireEventCallback(uint32_t playingId, WwiseCallbackType type, const std::string& markerLabel = {}) = 0;
    virtual bool completeBankLoad(uint32_t bankId, int32_t wwiseResult) = 0;
    virtual bool completeBankUnload(uint32_t bankId, int32_t wwiseResult) = 0;

    // Ordered log of every call made, for asserting Sec3.1/Sec3.2/Sec3.4
    // call-order fidelity against a null backend.
    virtual const std::vector<std::string>& callLog() const = 0;
};

// -----------------------------------------------------------------------
// NullWwiseBackend: a deterministic, in-process, no-engine implementation
// of the contract above. Every call succeeds unless a test configures a
// forced failure; nothing is actually played, decoded, streamed, or run on
// a real thread.
// -----------------------------------------------------------------------
class NullWwiseBackend : public WwiseBackend {
public:
    NullWwiseBackend() = default;

    bool memoryMgrInit(int32_t maxNumPools) override;
    bool streamMgrCreate(uint32_t memorySize) override;
    int32_t setupStreamDevice(const WwiseInitParams& params) override;
    bool soundEngineInit(const WwiseInitParams& params) override;
    bool musicEngineInit() override;
    void registerPlugin(const PluginRegistration& reg) override;
    void registerCodec(int32_t company, int32_t codecId) override;
    void motionEngineSetPlayerListener(int32_t player, int32_t listener) override;
    void setListenerPipeline(int32_t listener, bool audio, bool motion) override;
    void postCode(int32_t level) override;
    int32_t getSpeakerConfiguration() override;

    void musicEngineTerm() override;
    bool soundEngineIsInitialized() override;
    void stopAll(GameObjectId object) override;
    void clearPreparedEvents() override;
    void unregisterAllGameObj() override;
    void clearBanks() override;
    void soundEngineTerm() override;
    void setFileLocationResolver(bool toGameResolver) override;
    void destroyStreamDevice(int32_t deviceId) override;
    void streamMgrDestroyIfExists() override;
    void memoryMgrTerm() override;

    bool registerGameObj(uint64_t gameObjectHandle, const std::string& name) override;
    bool unregisterGameObj(uint64_t gameObjectHandle) override;
    bool setGameObjectEnvironmentsValues(GameObjectId object, const std::vector<AuxSend>& sends) override;
    bool setPosition(GameObjectId object, const SoundPosition& position, int32_t listenerIndex) override;
    bool setMultiplePositions(GameObjectId object, const std::vector<SoundPosition>& positions, int32_t type) override;
    bool setObjectObstructionAndOcclusion(GameObjectId object, int32_t listenerIndex, float obstruction, float occlusion) override;
    bool setRTPCValue(uint32_t rtpcId, float value, GameObjectId object) override;
    bool setSwitch(uint32_t switchGroup, uint32_t switchState, GameObjectId object) override;

    uint32_t postEvent(uint32_t eventId, GameObjectId object, uint32_t flags, PostEventCallback callback,
                        uint32_t cookie, const std::vector<ExternalSourceInfo>& externalSources) override;
    bool stopPlayingID(uint32_t playingId, int32_t fadeMs, int32_t curve) override;
    bool executeActionOnEvent(uint32_t eventId, EventAction action, GameObjectId object, int32_t fadeMs, int32_t curve) override;
    bool seekOnEvent(uint32_t eventId, GameObjectId object, int32_t positionMs, bool seekToNearestMarker) override;

    bool setListenerPosition(const ListenerPosition& position, int32_t listenerIndex) override;
    void renderAudio() override;
    bool setState(uint32_t stateGroup, uint32_t state) override;
    uint32_t getIDFromString(const std::string& name) override;

    bool loadBank(uint32_t bankId, BankCallback callback, uint32_t cookie, int32_t memoryPool) override;
    bool unloadBank(uint32_t bankId, BankCallback callback, uint32_t cookie) override;
    bool setBankLoadIOSettings(double throughput, int32_t priority) override;
    int32_t createPool(size_t size) override;
    bool setPoolName(int32_t poolId, const std::string& name) override;
    bool stopSourcePlugin(int32_t pluginId, int32_t company, uint32_t playingId) override;

    bool fireEventCallback(uint32_t playingId, WwiseCallbackType type, const std::string& markerLabel = {}) override;
    bool completeBankLoad(uint32_t bankId, int32_t wwiseResult) override;
    bool completeBankUnload(uint32_t bankId, int32_t wwiseResult) override;

    const std::vector<std::string>& callLog() const override { return log_; }

    // --- Test-only configuration (never consulted by real behaviour) ---
    void setForceRegisterGameObjResult(bool ok) { forceRegisterResult_ = ok; }
    void setForceSetRTPCValueResult(bool ok) { forceSetRtpcResult_ = ok; }
    void setForceSeekResult(bool ok) { forceSeekResult_ = ok; }
    // Forces the NEXT postEvent() call to fail (return playing id 0).
    void failNextPostEvent() { failNextPostEvent_ = true; }
    void setDeviceSchedulerRefusal(bool refuse) { refuseNonDeferredScheduler_ = refuse; }
    // Fails a specific numbered Wwise-init step (1-10, Sec3.2's table) to
    // exercise behaviour #13 (no rollback). 0 = don't fail any step.
    void setFailWwiseInitStep(int32_t step) { failInitStep_ = step; }

    bool memoryMgrInitialized() const { return memoryMgrInitialized_; }
    bool streamMgrCreated() const { return streamMgrCreated_; }
    bool deviceCreated() const { return deviceCreated_; }
    bool soundEngineInitialized_Flag() const { return soundEngineInitialized_; }
    bool musicEngineInitialized() const { return musicEngineInitialized_; }
    bool resolverSetToGame() const { return resolverSetToGame_; }

    // Sec10.2 (unit U-W3): the real CAkRTPCMgr-internal Switch apply table
    // and the separate State manager, now backing setSwitch()/setState()
    // below instead of a bare log-and-return-true stub. Exposed for tests
    // that want to inspect/seed the internal state directly (same
    // precedent as the accessors just above).
    SwitchGroupTable& switchTable() { return switchTable_; }
    const SwitchGroupTable& switchTable() const { return switchTable_; }
    StateGroupEngine& stateEngine() { return stateEngine_; }
    const StateGroupEngine& stateEngine() const { return stateEngine_; }

private:
    struct PendingEvent {
        PostEventCallback callback;
        uint32_t cookie = 0;
    };
    struct PendingBank {
        BankCallback callback;
        uint32_t cookie = 0;
    };

    void log(const std::string& entry) { log_.push_back(entry); }
    bool initStepOk(int32_t step) const { return failInitStep_ != step; }

    std::vector<std::string> log_;

    bool memoryMgrInitialized_ = false;
    bool streamMgrCreated_ = false;
    bool deviceCreated_ = false;
    int32_t deviceId_ = -1;
    bool soundEngineInitialized_ = false;
    bool musicEngineInitialized_ = false;
    bool resolverSetToGame_ = false;
    bool refuseNonDeferredScheduler_ = false;
    int32_t failInitStep_ = 0;

    bool forceRegisterResult_ = true;
    bool forceSetRtpcResult_ = true;
    bool forceSeekResult_ = true;
    bool failNextPostEvent_ = false;

    uint32_t nextPlayingId_ = 1;
    std::unordered_map<uint32_t, PendingEvent> pendingEvents_;
    std::map<uint32_t, PendingBank> pendingBankLoads_;
    std::map<uint32_t, PendingBank> pendingBankUnloads_;

    int32_t nextPoolId_ = 1;

    // Sec10.2 (unit U-W3): real internal apply state backing setSwitch()/
    // setState() (wwise_backend.cpp) - see the accessors above.
    SwitchGroupTable switchTable_;
    StateGroupEngine stateEngine_;
};

}  // namespace sr3audioruntime
