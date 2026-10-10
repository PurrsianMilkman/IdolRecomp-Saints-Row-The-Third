// Synthetic tests for the audio runtime (spec-audio-runtime.md, "Audio
// Runtime: the Wwise Adapter Contract"). Every test cites the spec section
// or behaviour number it exercises. These tests run against
// sr3audioruntime::NullWwiseBackend (a SpyBackend subclass below adds
// argument capture) - no real Wwise SDK, no real audio, no real threads;
// "the audio callback thread" is simulated by calling
// WwiseBackend::fireEventCallback()/completeBankLoad() directly and then
// AudioRuntime::pumpCallbackThread() to drain the queue, which is exactly
// the thread-crossing structure Sec4.5 describes (two steps, not one).
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "sr3audioruntime/audio_runtime.h"
#include "sr3audioruntime/io_scheduler.h"
#include "sr3audioruntime/switch_state_internal.h"
#include "sr3audioruntime/vorbis_container.h"
#include "sr3audioruntime/wwise_backend.h"
#include "sr3luahost/open_state.h"

using namespace sr3audioruntime;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                      \
    } while (0)

template <typename F>
bool throwsOpenStateError(F&& f, const char* needle = nullptr) {
    try {
        f();
    } catch (const sr3luahost::OpenStateError& e) {
        if (needle != nullptr && e.spec().find(needle) == std::string::npos &&
            e.global().find(needle) == std::string::npos) {
            std::cerr << "  (OPEN thrown but didn't name '" << needle << "': " << e.what() << ")\n";
            return false;
        }
        return true;
    }
    return false;
}

bool feq(float a, float b, float tol = 1e-4f) { return std::fabs(a - b) < tol; }

size_t indexOf(const std::vector<std::string>& log, const std::string& needle) {
    for (size_t i = 0; i < log.size(); ++i) {
        if (log[i].find(needle) != std::string::npos) return i;
    }
    return static_cast<size_t>(-1);
}

// A NullWwiseBackend subclass that also captures exact call arguments, for
// tests that need to inspect more than the call log's strings (behaviours
// #1, #2, #5, #9, #14).
class SpyBackend : public NullWwiseBackend {
public:
    std::vector<SoundPosition> lastMultiPositions;
    int multiPositionsCalls = 0;

    float lastObstructionSent = -1.0f;
    int obstructionCalls = 0;

    struct AuxCall {
        GameObjectId object;
        std::vector<AuxSend> sends;
    };
    std::vector<AuxCall> auxCalls;

    struct ExecuteActionCall {
        uint32_t eventId;
        EventAction action;
        GameObjectId object;
    };
    std::vector<ExecuteActionCall> executeActionCalls;

    struct PostEventCall {
        uint32_t eventId;
        GameObjectId object;
        uint32_t flags;
        std::vector<ExternalSourceInfo> externalSources;
    };
    std::vector<PostEventCall> postEventCalls;

    std::vector<uint32_t> stopPlayingIds;
    std::vector<uint32_t> switchObjectsSeen;  // just a count sentinel via group id

    bool setMultiplePositions(GameObjectId object, const std::vector<SoundPosition>& positions, int32_t type) override {
        ++multiPositionsCalls;
        lastMultiPositions = positions;
        return NullWwiseBackend::setMultiplePositions(object, positions, type);
    }
    bool setObjectObstructionAndOcclusion(GameObjectId object, int32_t listenerIndex, float obstruction,
                                           float occlusion) override {
        ++obstructionCalls;
        lastObstructionSent = obstruction;
        return NullWwiseBackend::setObjectObstructionAndOcclusion(object, listenerIndex, obstruction, occlusion);
    }
    bool setGameObjectEnvironmentsValues(GameObjectId object, const std::vector<AuxSend>& sends) override {
        auxCalls.push_back({object, sends});
        return NullWwiseBackend::setGameObjectEnvironmentsValues(object, sends);
    }
    bool executeActionOnEvent(uint32_t eventId, EventAction action, GameObjectId object, int32_t fadeMs,
                               int32_t curve) override {
        executeActionCalls.push_back({eventId, action, object});
        return NullWwiseBackend::executeActionOnEvent(eventId, action, object, fadeMs, curve);
    }
    uint32_t postEvent(uint32_t eventId, GameObjectId object, uint32_t flags, PostEventCallback callback,
                        uint32_t cookie, const std::vector<ExternalSourceInfo>& externalSources) override {
        postEventCalls.push_back({eventId, object, flags, externalSources});
        return NullWwiseBackend::postEvent(eventId, object, flags, std::move(callback), cookie, externalSources);
    }
    bool stopPlayingID(uint32_t playingId, int32_t fadeMs, int32_t curve) override {
        stopPlayingIds.push_back(playingId);
        return NullWwiseBackend::stopPlayingID(playingId, fadeMs, curve);
    }
};

AudioLibraryConfig validConfig() {
    AudioLibraryConfig cfg;
    return cfg;  // defaults already pass every "must be non-zero" check
}

// ---------------------------------------------------------------------
// Behaviour #1: SetMultiplePositions orientation corruption (Sec5 #1).
// ---------------------------------------------------------------------
void testBehaviour1_OrientationCorruption() {
    SpyBackend backend;
    AudioRuntime rt(backend);
    CHECK(rt.init(validConfig()));
    GameObjectId v = rt.createVoice("v1");
    Voice* voice = rt.findVoice(v);
    CHECK(voice != nullptr);
    voice->positions.push_back(SoundPosition{1, 2, 3, 10, 20, 30});
    voice->positions.push_back(SoundPosition{4, 5, 6, 40, 50, 60});

    rt.tickAllVoices();

    CHECK(backend.multiPositionsCalls == 1);
    CHECK(backend.lastMultiPositions.size() == 2);
    // Positions copied correctly.
    CHECK(feq(backend.lastMultiPositions[0].posX, 1) && feq(backend.lastMultiPositions[0].posY, 2) &&
          feq(backend.lastMultiPositions[0].posZ, 3));
    CHECK(feq(backend.lastMultiPositions[1].posX, 4) && feq(backend.lastMultiPositions[1].posY, 5) &&
          feq(backend.lastMultiPositions[1].posZ, 6));
    // The bug: orientation x ends up = source orientation z; y/z are not
    // the source's real orientation y/z (the real engine leaves stale
    // stack garbage there; this project leaves 0, documented as the one
    // unavoidable difference from a real uninitialised frame).
    CHECK(feq(backend.lastMultiPositions[0].oriX, 30));  // == source oriZ, NOT source oriX (10)
    CHECK(feq(backend.lastMultiPositions[1].oriX, 60));  // == source oriZ, NOT source oriX (40)
}

// ---------------------------------------------------------------------
// Behaviour #2: no bound on multi-position count; guarded at 16 (Sec5 #2).
// ---------------------------------------------------------------------
void testBehaviour2_MultiPositionGuardAt16() {
    SpyBackend backend;
    AudioRuntime rt(backend);
    CHECK(rt.init(validConfig()));
    GameObjectId v = rt.createVoice("v1");
    Voice* voice = rt.findVoice(v);
    for (int i = 0; i < 20; ++i) {
        voice->positions.push_back(SoundPosition{static_cast<float>(i), 0, 0, 0, 0, 0});
    }
    rt.tickAllVoices();
    CHECK(backend.multiPositionsCalls == 1);
    CHECK(backend.lastMultiPositions.size() == 16);  // guarded, not 20 (would overrun the real 16-slot frame)
    for (int i = 0; i < 16; ++i) {
        CHECK(feq(backend.lastMultiPositions[static_cast<size_t>(i)].posX, static_cast<float>(i)));
    }
}

// ---------------------------------------------------------------------
// Behaviour #3: voice-line post has no engine-up guard, leaves +0x1e=1 on
// failure; the sound-effect path clears it (Sec5 #3).
// ---------------------------------------------------------------------
void testBehaviour3_VoiceLineNoEngineGuardAndPostedFlag() {
    // Part A: the call happens even with the engine down.
    {
        SpyBackend backend;
        AudioRuntime rt(backend);  // init() never called: engine stays down
        GameObjectId v = rt.createVoice("v1");
        Voice* voice = rt.findVoice(v);
        voice->flags &= static_cast<uint16_t>(~voice_flags::kNeedsRegister);  // isolate from behaviour #10
        CHECK(rt.postPlayRecord(v, /*cookie*/ 1, /*lineKey*/ 777, /*isVoiceLine*/ true));
        rt.tickAllVoices();
        CHECK(backend.postEventCalls.size() == 1);  // called despite engineUp() == false
        CHECK(!rt.engineUp());
    }
    // Part B: forced post failure leaves +0x1e (posted) at 1 for the
    // voice-line path, but clears it for the sound-effect path.
    {
        SpyBackend backend;
        AudioRuntime rt(backend);
        CHECK(rt.init(validConfig()));
        GameObjectId v = rt.createVoice("v1");

        backend.failNextPostEvent();
        CHECK(rt.postPlayRecord(v, 10, 111, /*isVoiceLine*/ true));
        rt.tickAllVoices();
        PlayRecord* voiceLineRec = rt.findPlayRecord(v, 10);
        CHECK(voiceLineRec != nullptr);
        CHECK(voiceLineRec->posted == true);  // left at 1 on failure
        CHECK(voiceLineRec->playingId == 0);

        backend.failNextPostEvent();
        CHECK(rt.postPlayRecord(v, 20, 222, /*isVoiceLine*/ false));
        rt.tickAllVoices();
        PlayRecord* soundEffectRec = rt.findPlayRecord(v, 20);
        CHECK(soundEffectRec != nullptr);
        CHECK(soundEffectRec->posted == false);  // cleared on sound-effect failure
    }
}

// ---------------------------------------------------------------------
// Behaviour #4: voice teardown does not stop its events (Sec5 #4).
// ---------------------------------------------------------------------
void testBehaviour4_TeardownDoesNotStopEvents() {
    SpyBackend backend;
    AudioRuntime rt(backend);
    CHECK(rt.init(validConfig()));
    GameObjectId v = rt.createVoice("v1");
    CHECK(rt.postPlayRecord(v, 1, 100, false));
    rt.tickAllVoices();  // posts the record, playingId assigned
    PlayRecord* rec = rt.findPlayRecord(v, 1);
    CHECK(rec != nullptr && rec->playingId != 0);

    rt.destroyVoiceNow(v);

    CHECK(backend.stopPlayingIds.empty());  // no StopPlayingID issued for the still-posted record
    CHECK(indexOf(backend.callLog(), "UnregisterGameObj") != static_cast<size_t>(-1));
}

// ---------------------------------------------------------------------
// Behaviour #5: pause/resume are event-wide on the object, not per playing
// id (Sec5 #5).
// ---------------------------------------------------------------------
void testBehaviour5_PauseResumeEventWide() {
    SpyBackend backend;
    AudioRuntime rt(backend);
    CHECK(rt.init(validConfig()));
    GameObjectId v = rt.createVoice("v1");
    CHECK(rt.postPlayRecord(v, 1, 500, false));
    CHECK(rt.postPlayRecord(v, 2, 500, false));  // SAME event id on the same voice
    rt.tickAllVoices();                           // posts both
    uint32_t playingId1 = rt.findPlayRecord(v, 1)->playingId;
    uint32_t playingId2 = rt.findPlayRecord(v, 2)->playingId;
    CHECK(playingId1 != 0 && playingId2 != 0 && playingId1 != playingId2);
    backend.fireEventCallback(playingId1, WwiseCallbackType::kDuration);
    backend.fireEventCallback(playingId2, WwiseCallbackType::kDuration);
    rt.pumpCallbackThread();
    rt.tickAllVoices();  // both records -> Playing

    rt.requestPause(v, 1);  // only record 1 asks
    rt.tickAllVoices();

    // The underlying Wwise call targets (event, object) - no playing id at
    // all - so it is structurally "every instance of this event on this
    // voice", exactly as the spec states.
    CHECK(backend.executeActionCalls.size() == 1);
    CHECK(backend.executeActionCalls[0].eventId == 500);
    CHECK(backend.executeActionCalls[0].object == v);
    // Only the record that asked changes its OWN tracked state.
    CHECK(rt.findPlayRecord(v, 1)->state == PlayRecordState::kPauseSent);
    CHECK(rt.findPlayRecord(v, 2)->state == PlayRecordState::kPlaying);
}

// ---------------------------------------------------------------------
// Behaviour #6: smoothed global RTPC 0xdb2d51a3's asymmetric decay/snap
// (Sec5 #6).
// ---------------------------------------------------------------------
void testBehaviour6_SmoothedRtpcDecayAndSnap() {
    {
        // Decay branch throws until k is set (Sec6 OPEN item 7).
        NullWwiseBackend backend;
        AudioRuntime rt(backend);
        rt.smoothedRtpc().target = 0.0f;
        rt.smoothedRtpc().current = 1.0f;  // exceeds target by > 0.05
        CHECK(throwsOpenStateError([&] { rt.smoothedRtpc().tick(rt.rtpcCache(), backend, true); }, "item 7"));
    }
    {
        NullWwiseBackend backend;
        AudioRuntime rt(backend);
        rt.smoothedRtpc().k().set(0.5f);
        rt.smoothedRtpc().target = 0.0f;
        rt.smoothedRtpc().current = 1.0f;
        rt.smoothedRtpc().tick(rt.rtpcCache(), backend, true);
        CHECK(feq(rt.smoothedRtpc().current, 0.5f));  // (1.0-0.0)*0.5
        rt.smoothedRtpc().tick(rt.rtpcCache(), backend, true);
        CHECK(feq(rt.smoothedRtpc().current, 0.25f));  // still > 0.05 above target -> decay again
    }
    {
        // Non-zero target: first step can overshoot BELOW target, then the
        // next tick snaps straight to it (Sec5 #6's documented quirk).
        NullWwiseBackend backend;
        AudioRuntime rt(backend);
        rt.smoothedRtpc().k().set(0.1f);
        rt.smoothedRtpc().target = 0.5f;
        rt.smoothedRtpc().current = 1.0f;  // exceeds target(0.5) by 0.5 > 0.05
        rt.smoothedRtpc().tick(rt.rtpcCache(), backend, true);
        CHECK(feq(rt.smoothedRtpc().current, 0.05f));  // (1.0-0.5)*0.1, now BELOW target
        rt.smoothedRtpc().tick(rt.rtpcCache(), backend, true);
        CHECK(feq(rt.smoothedRtpc().current, 0.5f));  // snapped straight to target
    }
    {
        // Growth (current < target) never touches k.
        NullWwiseBackend backend;
        AudioRuntime rt(backend);
        rt.smoothedRtpc().target = 1.0f;
        rt.smoothedRtpc().current = 0.2f;
        bool threw = false;
        try {
            rt.smoothedRtpc().tick(rt.rtpcCache(), backend, true);
        } catch (const sr3luahost::OpenStateError&) {
            threw = true;
        }
        CHECK(!threw);
        CHECK(feq(rt.smoothedRtpc().current, 1.0f));  // snapped up to target
    }
}

// ---------------------------------------------------------------------
// Behaviour #7: the global RTPC cache drops sub-epsilon changes and, once
// full, drops everything (Sec5 #7).
// ---------------------------------------------------------------------
void testBehaviour7_GlobalRtpcCacheDrops() {
    NullWwiseBackend backend;
    GlobalRtpcCache cache;
    CHECK(cache.trySend(backend, 1, 1.0f, true));              // first sight: always sent
    CHECK(!cache.trySend(backend, 1, 1.0005f, true));           // delta 0.0005 < 0.001 epsilon: dropped
    CHECK(cache.trySend(backend, 1, 1.01f, true));              // delta > epsilon: sent
    CHECK(!cache.trySend(backend, 1, 1.01f, true));             // unchanged: dropped

    GlobalRtpcCache full;
    for (uint32_t id = 0; id < GlobalRtpcCache::kCapacity; ++id) {
        CHECK(full.trySend(backend, id, 1.0f, true));  // fills every one of the 200 slots
    }
    CHECK(!full.trySend(backend, 99999u, 1.0f, true));  // table full of OTHER ids: "unchanged", not sent

    // engineUp == false: the cache is not even consulted (no insert happens).
    GlobalRtpcCache gated;
    CHECK(!gated.trySend(backend, 5, 1.0f, false));
    CHECK(gated.trySend(backend, 5, 1.0f, true));  // still unseen - proves the engine-down call above never inserted
}

// ---------------------------------------------------------------------
// Behaviour #8: the listener is not sent every frame, only while the arm
// counter is positive (Sec5 #8).
// ---------------------------------------------------------------------
void testBehaviour8_ListenerArmCounterGating() {
    NullWwiseBackend backend;
    ListenerState listener;
    CHECK(!listener.tick(backend, true));  // counter 0: nothing sent, no OPEN throw even though position unset
    listener.position().set(ListenerPosition{});
    listener.arm(2);
    CHECK(listener.tick(backend, true));
    CHECK(listener.armCounter == 1);
    CHECK(listener.tick(backend, true));
    CHECK(listener.armCounter == 0);
    CHECK(!listener.tick(backend, true));  // armed frames exhausted: no further send
    // Decremented even when the engine is down.
    listener.arm(1);
    CHECK(!listener.tick(backend, false));  // engine down: no Wwise call...
    CHECK(listener.armCounter == 0);        // ...but the counter still dropped to 0
}

// ---------------------------------------------------------------------
// Behaviour #9: per-voice positional updates are time-gated, not
// frame-gated; obstruction slews at most 7/100 per update (Sec5 #9).
// ---------------------------------------------------------------------
void testBehaviour9_TimeGatedPositionalAndObstructionSlew() {
    SpyBackend backend;
    AudioRuntime rt(backend);
    CHECK(rt.init(validConfig()));
    GameObjectId v = rt.createVoice("v1");
    Voice* voice = rt.findVoice(v);
    voice->positions.push_back(SoundPosition{1, 0, 0, 0, 0, 0});
    voice->obstructionTarget = 100;

    rt.tickAllVoices();  // due at t=0: sends once, obstruction moves 0 -> 7
    CHECK(backend.obstructionCalls == 1);
    CHECK(feq(backend.lastObstructionSent, 7.0f / 100.0f));

    int callsBefore = backend.obstructionCalls;
    rt.tickAllVoices();  // no time advanced: still gated by the 33 ms deadline (voice has a play record... actually none; idle 5s)
    CHECK(backend.obstructionCalls == callsBefore);  // time-gated, NOT frame-gated: a second tick with no elapsed time does nothing

    rt.advanceTimeMs(5000);  // idle deadline (no play records): 5000 ms
    rt.tickAllVoices();
    CHECK(backend.obstructionCalls == callsBefore + 1);
    CHECK(feq(backend.lastObstructionSent, 14.0f / 100.0f));  // 7 -> 14, at most 7/100 per update

    // "no call at all when already at target"
    voice->obstructionTarget = voice->obstructionSent;
    rt.advanceTimeMs(5000);
    int callsBeforeNoop = backend.obstructionCalls;
    rt.tickAllVoices();
    CHECK(backend.obstructionCalls == callsBeforeNoop);
}

// ---------------------------------------------------------------------
// Behaviour #10: one-tick latencies (Sec5 #10), in three parts.
// ---------------------------------------------------------------------
void testBehaviour10a_SwitchChangeSkipsRecordsThisTick() {
    SpyBackend backend;
    AudioRuntime rt(backend);
    CHECK(rt.init(validConfig()));
    GameObjectId v = rt.createVoice("v1");
    Voice* voice = rt.findVoice(v);
    voice->flags &= static_cast<uint16_t>(~voice_flags::kNeedsRegister);
    voice->switches.push_back(SwitchNode{1, 2, /*dirty*/ true});
    voice->flags |= voice_flags::kSwitchListDirty;
    CHECK(rt.postPlayRecord(v, 1, 100, false));

    rt.tickAllVoices();
    CHECK(indexOf(backend.callLog(), "SetSwitch(1,2") != static_cast<size_t>(-1));
    PlayRecord* rec = rt.findPlayRecord(v, 1);
    CHECK(rec != nullptr && rec->playingId == 0 && rec->state == PlayRecordState::kCreated);  // untouched this tick

    rt.tickAllVoices();  // switch no longer dirty: record processes normally now
    CHECK(rt.findPlayRecord(v, 1)->playingId != 0);
}

void testBehaviour10b_LazyRegisterSkipsTick() {
    SpyBackend backend;
    AudioRuntime rt(backend);
    CHECK(rt.init(validConfig()));
    GameObjectId v = rt.createVoice("v1");  // registered synchronously; force it back to "needs register"
    Voice* voice = rt.findVoice(v);
    voice->flags |= voice_flags::kNeedsRegister;
    CHECK(rt.postPlayRecord(v, 1, 100, false));

    rt.tickAllVoices();  // this tick only (re)registers; "either way go to the next voice"
    PlayRecord* rec = rt.findPlayRecord(v, 1);
    CHECK(rec != nullptr && rec->playingId == 0);
    CHECK((rt.findVoice(v)->flags & voice_flags::kNeedsRegister) == 0);

    rt.tickAllVoices();
    CHECK(rt.findPlayRecord(v, 1)->playingId != 0);
}

void testBehaviour10c_NewRecordPlayingOnlyAfterDurationCrossesQueue() {
    SpyBackend backend;
    AudioRuntime rt(backend);
    CHECK(rt.init(validConfig()));
    GameObjectId v = rt.createVoice("v1");
    CHECK(rt.postPlayRecord(v, 1, 100, false));

    rt.tickAllVoices();  // posts; stays state 1 ("created") on success
    uint32_t playingId = rt.findPlayRecord(v, 1)->playingId;
    CHECK(playingId != 0);
    CHECK(rt.findPlayRecord(v, 1)->state == PlayRecordState::kCreated);

    backend.fireEventCallback(playingId, WwiseCallbackType::kDuration);  // queues status 4
    CHECK(rt.findPlayRecord(v, 1)->state == PlayRecordState::kCreated);  // queued, not yet applied

    rt.pumpCallbackThread();  // applies the request bit, but the STATE MACHINE hasn't run again yet
    CHECK(rt.findPlayRecord(v, 1)->state == PlayRecordState::kCreated);

    rt.tickAllVoices();  // only now does it become "playing"
    CHECK(rt.findPlayRecord(v, 1)->state == PlayRecordState::kPlaying);
}

// ---------------------------------------------------------------------
// Behaviour #11: statuses can be lost when the message pool is exhausted
// (Sec5 #11).
// ---------------------------------------------------------------------
void testBehaviour11_MessagePoolExhaustion() {
    NullWwiseBackend backend;
    AudioRuntime rt(backend, /*messagePoolCapacity*/ 2);
    CHECK(rt.postStatusMessage(status::kStarted, 1));
    CHECK(rt.postStatusMessage(status::kStarted, 2));
    CHECK(!rt.postStatusMessage(status::kStarted, 3));  // pool exhausted: dropped
    CHECK(rt.droppedMessageCount() == 1);
    CHECK(rt.queuedMessageCount() == 2);
}

// ---------------------------------------------------------------------
// Behaviour #12: bank callbacks report the bank id, not the cookie passed
// to LoadBank (Sec5 #12).
// ---------------------------------------------------------------------
void testBehaviour12_BankCallbackReportsBankIdNotCookie() {
    NullWwiseBackend backend;
    AudioRuntime rt(backend);
    CHECK(rt.init(validConfig()));
    rt.registerBank(42);
    CHECK(rt.requestLoadBank(42));
    const BankRecord* rec = rt.findBank(42);
    CHECK(rec != nullptr);
    CHECK(rec->wwiseLoadCookie != 42);  // the Wwise-level cookie is NOT the bank id

    CHECK(backend.completeBankLoad(42, 1));  // simulate Wwise's bank thread finishing
    rt.pumpCallbackThread();
    CHECK(rt.findBank(42)->resident == true);
    CHECK(rt.findBank(42)->idle == false);

    // Sec1.4 0x00465cc0: "Only when byte 0x031728c5 is set; otherwise ...
    // the record gets error bit 1" - the synchronous call-site error, not
    // the async Sec3.7 completion path exercised above.
    NullWwiseBackend downBackend;
    AudioRuntime downRt(downBackend);  // init() never called: engine stays down
    downRt.registerBank(7);
    CHECK(!downRt.requestLoadBank(7));
    CHECK(downRt.findBank(7)->errorFlag == true);
}

// ---------------------------------------------------------------------
// Behaviour #13: init has no rollback (Sec5 #13).
// ---------------------------------------------------------------------
void testBehaviour13_InitNoRollback() {
    NullWwiseBackend backend;
    backend.setFailWwiseInitStep(4);  // SoundEngine::Init fails
    AudioRuntime rt(backend);
    CHECK(rt.init(validConfig()));  // the audio-library init itself still "succeeds" structurally
    CHECK(!rt.engineUp());          // but Wwise never came up
    // Steps 1-3 already ran and are NOT unwound.
    CHECK(backend.memoryMgrInitialized());
    CHECK(backend.streamMgrCreated());
    CHECK(backend.deviceCreated());
    // Step 4 itself failed, so nothing past it ran.
    CHECK(!backend.musicEngineInitialized());
    // Later audio-library steps (8-14) still ran despite the Wwise failure - no abort, no rollback.
    CHECK(indexOf(rt.stepLog(), "step14:default-voice-created") != static_cast<size_t>(-1));
}

// ---------------------------------------------------------------------
// Behaviour #14: codec 4 (Vorbis) only; voice lines addressed by file id,
// not name (Sec5 #14).
// ---------------------------------------------------------------------
void testBehaviour14_CodecVorbisFileIdAddressing() {
    SpyBackend backend;
    AudioRuntime rt(backend);
    CHECK(rt.init(validConfig()));
    GameObjectId v = rt.createVoice("v1");
    CHECK(rt.postPlayRecord(v, 1, /*lineKey*/ 9001, /*isVoiceLine*/ true));
    rt.tickAllVoices();

    CHECK(backend.postEventCalls.size() == 1);
    const auto& call = backend.postEventCalls[0];
    CHECK(call.externalSources.size() == 1);
    CHECK(call.externalSources[0].codec == 4);               // Vorbis, always
    CHECK(call.externalSources[0].fileId == 9001u);          // addressed by file id
    CHECK(call.flags == (post_event_flags::kEndOfEvent | post_event_flags::kDuration));  // flags 9, no marker bit
    // RegisterCodec(0,4) happened during init (also proves codec 4 is the only one registered here).
    CHECK(indexOf(backend.callLog(), "RegisterCodec(company=0,codec=4)") != static_cast<size_t>(-1));
}

// ---------------------------------------------------------------------
// Full play-record lifecycle (Sec2.1 / Sec4): created -> posted -> playing
// -> pause sent -> paused -> playing -> stop sent -> ended -> freed.
// ---------------------------------------------------------------------
void testPlayRecordFullLifecycle() {
    SpyBackend backend;
    AudioRuntime rt(backend);
    CHECK(rt.init(validConfig()));
    GameObjectId v = rt.createVoice("v1");
    CHECK(rt.postPlayRecord(v, 1, 42, false));

    rt.tickAllVoices();
    PlayRecord* rec = rt.findPlayRecord(v, 1);
    CHECK(rec->state == PlayRecordState::kCreated && rec->playingId != 0 && rec->posted);

    backend.fireEventCallback(rec->playingId, WwiseCallbackType::kDuration);
    rt.pumpCallbackThread();
    rt.tickAllVoices();
    CHECK(rt.findPlayRecord(v, 1)->state == PlayRecordState::kPlaying);
    CHECK(rt.findPlayRecord(v, 1)->hasStarted);

    rt.requestPause(v, 1);
    rt.tickAllVoices();
    CHECK(rt.findPlayRecord(v, 1)->state == PlayRecordState::kPauseSent);

    rt.pumpCallbackThread();  // status 6 was queued by the pause wrapper itself
    rt.tickAllVoices();
    CHECK(rt.findPlayRecord(v, 1)->state == PlayRecordState::kPaused);

    rt.requestResume(v, 1);
    rt.tickAllVoices();
    CHECK(rt.findPlayRecord(v, 1)->state == PlayRecordState::kPaused);  // "stays 4" synchronously

    rt.pumpCallbackThread();  // status 4 queued by the resume wrapper
    rt.tickAllVoices();
    CHECK(rt.findPlayRecord(v, 1)->state == PlayRecordState::kPlaying);

    rt.requestStop(v, 1);
    rt.tickAllVoices();
    CHECK(rt.findPlayRecord(v, 1)->state == PlayRecordState::kStopSent);

    rt.pumpCallbackThread();  // status 7 queued by the stop wrapper
    rt.tickAllVoices();       // phase 1 consumes it -> state 6, bit 31 set -> unlinked and freed
    CHECK(rt.findPlayRecord(v, 1) == nullptr);
}

// ---------------------------------------------------------------------
// Regression: a record that reaches a terminal state via the STOP
// wrapper's own synchronous path (not via phase 1's status-bit
// consumption) must still be unlinked/freed the same tick - Sec2.1:
// "States 6, 7, 8 on entry set flag bit 31 and return; the voice tick
// then unlinks and frees the record." applies regardless of which path
// got it there.
// ---------------------------------------------------------------------
void testStopOnFailedRecordDoesNotLeak() {
    SpyBackend backend;
    AudioRuntime rt(backend);
    CHECK(rt.init(validConfig()));
    GameObjectId v = rt.createVoice("v1");
    backend.failNextPostEvent();
    CHECK(rt.postPlayRecord(v, 1, 100, false));
    rt.tickAllVoices();  // post fails -> state Failed (Sec1.2)
    CHECK(rt.findPlayRecord(v, 1) != nullptr);
    CHECK(rt.findPlayRecord(v, 1)->state == PlayRecordState::kFailed);

    rt.requestStop(v, 1);
    rt.tickAllVoices();  // Sec2.3: "state 7 -> 6", must be unlinked this same tick
    CHECK(rt.findPlayRecord(v, 1) == nullptr);
}

// ---------------------------------------------------------------------
// The `.ctdg_pc` active-conversation pool (spec-conversation-format.md
// Sec4-Sec6; spec-audio-runtime.md Sec1.2a). CORRECTED 2026-10-08: the
// three `0x58`-byte entries PlayRecord::conversationSlot (+0x1c) back-
// references were previously mis-modelled as "persona-line (.lm_pc)
// streaming slots" - this is the real mechanism: exactly
// kConversationPoolSize (3) slots, acquire-or-fail on exhaustion (no
// spec text describes any other exhaustion policy, so none is invented -
// see acquireConversationSlot()'s own doc comment), and releasing a slot
// force-stops every play record still referencing it, not just whichever
// one triggered the release.
// ---------------------------------------------------------------------
void testConversationPool_AcquireReleaseExhaustionAndForceStopCascade() {
    SpyBackend backend;
    AudioRuntime rt(backend);
    CHECK(rt.init(validConfig()));
    GameObjectId v1 = rt.createVoice("v1");
    GameObjectId v2 = rt.createVoice("v2");

    // Fill all kConversationPoolSize (3) slots.
    CHECK(rt.postConversationTurn(v1, /*cookie*/ 1, /*lineKey*/ 100));
    CHECK(rt.postConversationTurn(v1, /*cookie*/ 2, /*lineKey*/ 101));
    CHECK(rt.postConversationTurn(v1, /*cookie*/ 3, /*lineKey*/ 102));
    CHECK(rt.conversationSlotBusy(0) && rt.conversationSlotBusy(1) && rt.conversationSlotBusy(2));

    // Exhaustion: a 4th request fails outright and posts nothing at all
    // (no play record is left behind for the rejected cookie).
    CHECK(!rt.postConversationTurn(v1, /*cookie*/ 4, /*lineKey*/ 103));
    CHECK(rt.findPlayRecord(v1, 4) == nullptr);

    uint8_t slot1 = rt.findPlayRecord(v1, 1)->conversationSlot;
    uint8_t slot2 = rt.findPlayRecord(v1, 2)->conversationSlot;
    uint8_t slot3 = rt.findPlayRecord(v1, 3)->conversationSlot;
    CHECK(slot1 < kConversationPoolSize && slot2 < kConversationPoolSize && slot3 < kConversationPoolSize);
    CHECK(slot1 != slot2 && slot2 != slot3 && slot1 != slot3);  // three genuinely distinct slots

    rt.tickAllVoices();  // posts all three (voice-line path: "stays in state 1 on success")
    CHECK(rt.findPlayRecord(v1, 1)->posted && rt.findPlayRecord(v1, 1)->playingId != 0);

    // Stopping ONE record eventually releases ITS slot, following the
    // same stop-to-unlink dance testPlayRecordFullLifecycle already
    // establishes (Sec2.3: "release stream slot" fires on the stop
    // wrapper's own bit, Sec2.1's +0x1c row).
    rt.requestStop(v1, 1);
    rt.tickAllVoices();  // -> StopSent
    CHECK(rt.findPlayRecord(v1, 1)->state == PlayRecordState::kStopSent);
    CHECK(!rt.conversationSlotBusy(slot1));  // released as soon as the stop bit was handled, not on unlink
    rt.pumpCallbackThread();  // status 7 queued by the stop wrapper
    rt.tickAllVoices();       // phase 1 consumes it -> state 6 -> unlinked
    CHECK(rt.findPlayRecord(v1, 1) == nullptr);

    // The freed slot is reusable by a brand-new conversation.
    CHECK(rt.postConversationTurn(v1, /*cookie*/ 5, /*lineKey*/ 104));
    CHECK(rt.findPlayRecord(v1, 5)->conversationSlot == slot1);

    // Force-stop cascade: two records on TWO DIFFERENT voices made to
    // share one conversation's slot (modelling two speakers' lines inside
    // the same active conversation) both get their stop bit raised when
    // that slot is released directly - not just whichever record
    // happened to trigger the release (Sec1.2a: "force-stops any live
    // play record still referencing the slot before freeing it").
    CHECK(rt.postPlayRecord(v2, /*cookie*/ 6, /*lineKey*/ 200, /*isVoiceLine*/ true));
    rt.tickAllVoices();  // posts it (same "stays in state 1, posted=true" shape as above)
    CHECK(rt.findPlayRecord(v2, 6)->posted);
    rt.findPlayRecord(v2, 6)->conversationSlot = slot2;  // test-only: 2nd speaker sharing slot2

    rt.releaseConversationSlot(slot2);
    CHECK(!rt.conversationSlotBusy(slot2));
    CHECK((rt.findPlayRecord(v1, 2)->requestWord & request_bits::kStop) != 0);
    CHECK((rt.findPlayRecord(v2, 6)->requestWord & request_bits::kStop) != 0);

    rt.tickAllVoices();        // both -> StopSent (both were posted/state kCreated+posted already)
    rt.pumpCallbackThread();   // both status-7 messages
    rt.tickAllVoices();        // both unlinked
    CHECK(rt.findPlayRecord(v1, 2) == nullptr);
    CHECK(rt.findPlayRecord(v2, 6) == nullptr);

    // Teardown releases any slot still held by that voice's records
    // WITHOUT cascading a force-stop into other voices (Sec1.1 0x0045cc10
    // / behaviour #4's own "without issuing StopPlayingID").
    CHECK(rt.postConversationTurn(v1, /*cookie*/ 7, /*lineKey*/ 105));
    uint8_t slot7 = rt.findPlayRecord(v1, 7)->conversationSlot;
    CHECK(rt.conversationSlotBusy(slot7));

    // Give v2 a record sharing that same slot, to actually distinguish
    // "freed directly" from "freed via the force-stop cascade": a real
    // cascade (releaseConversationSlot, exercised above) would raise
    // THIS record's stop bit too; teardown must not.
    CHECK(rt.postPlayRecord(v2, /*cookie*/ 8, /*lineKey*/ 300, /*isVoiceLine*/ true));
    rt.findPlayRecord(v2, 8)->conversationSlot = slot7;

    rt.destroyVoiceNow(v1);
    CHECK(!rt.conversationSlotBusy(slot7));
    CHECK((rt.findPlayRecord(v2, 8)->requestWord & request_bits::kStop) == 0);  // NOT cascaded
}

// ---------------------------------------------------------------------
// Init/shutdown call-order contracts (Sec3.1/Sec3.2/Sec3.4).
// ---------------------------------------------------------------------
void testInitShutdownOrder() {
    NullWwiseBackend backend;
    AudioRuntime rt(backend);
    CHECK(rt.init(validConfig()));

    const auto& steps = rt.stepLog();
    size_t s1 = indexOf(steps, "step1:");
    size_t s6 = indexOf(steps, "step6:audio-callback-thread-started");
    size_t s7 = indexOf(steps, "step7:wwise-init-begin");
    size_t s8 = indexOf(steps, "step8:bank-system-reset");
    size_t s9 = indexOf(steps, "step9:voice-pools-built");
    size_t s11 = indexOf(steps, "step11:main-audio-thread-started");
    size_t s14 = indexOf(steps, "step14:default-voice-created");
    CHECK(s1 < s6 && s6 < s7 && s7 < s8 && s8 < s9 && s9 < s11 && s11 < s14);  // Sec3.1 order

    const auto& wlog = backend.callLog();
    size_t mem = indexOf(wlog, "MemoryMgr::Init");
    size_t strm = indexOf(wlog, "StreamMgr::Create");
    size_t dev = indexOf(wlog, "setupStreamDevice");
    size_t snd = indexOf(wlog, "SoundEngine::Init");
    size_t music = indexOf(wlog, "MusicEngine::Init");
    size_t plug = indexOf(wlog, "RegisterPlugin(type=3,company=0,id=115");  // first plugin, Sec3.2's table order
    size_t codec = indexOf(wlog, "RegisterCodec");
    size_t speaker = indexOf(wlog, "GetSpeakerConfiguration");
    CHECK(mem < strm && strm < dev && dev < snd && snd < music && music < plug && plug < codec && codec < speaker);

    rt.shutdown();
    const auto& wlog2 = backend.callLog();
    size_t musicTerm = indexOf(wlog2, "MusicEngine::Term");
    size_t stopAll = indexOf(wlog2, "StopAll");
    size_t clearBanks = indexOf(wlog2, "ClearBanks");
    size_t sndTerm = indexOf(wlog2, "SoundEngine::Term");
    size_t resolverNull = indexOf(wlog2, "SetFileLocationResolver(null)");
    size_t destroyDev = indexOf(wlog2, "DestroyDevice");
    size_t memTerm = indexOf(wlog2, "MemoryMgr::Term");
    CHECK(musicTerm != static_cast<size_t>(-1) && musicTerm < stopAll && stopAll < clearBanks &&
          clearBanks < sndTerm && sndTerm < resolverNull && resolverNull < destroyDev && destroyDev < memTerm);

    const auto& steps2 = rt.stepLog();
    size_t sh1 = indexOf(steps2, "shutdown1:mix-rtpc-off");
    size_t sh5 = indexOf(steps2, "shutdown5:engine-up-cleared");
    size_t sh6 = indexOf(steps2, "shutdown6:audio-callback-thread-stop-signal");
    size_t sh7 = indexOf(steps2, "shutdown7:voiceapi-down");
    CHECK(sh1 < sh5 && sh5 < sh6 && sh6 < sh7);
    CHECK(!rt.engineUp());
}

// ---------------------------------------------------------------------
// Unit U-W3 (spec-audio-runtime.md Sec10.2): Switch - CAkRTPCMgr's
// un-exported SetSwitchInternal is pure bookkeeping with NO "unchanged"
// short-circuit (Sec10.2.2), unlike RTPC/State's own exact-equality checks.
// ---------------------------------------------------------------------
void testUW3Switch_AlwaysNotifiesNoChangeFilter() {
    SwitchGroupTable table;
    CHECK(throwsOpenStateError([&] { table.currentSwitch(1, 7); }, "34"));  // OPEN item 34, no default
    CHECK(!table.hasSwitch(1, 7));

    int notifyCount = 0;
    GameObjectId lastObj = 0;
    uint32_t lastState = 0xffffffffu;
    table.registerGroupWatcher(7, [&](GameObjectId obj, uint32_t state) {
        ++notifyCount;
        lastObj = obj;
        lastState = state;
    });

    table.setSwitch(1, 7, 3);
    CHECK(notifyCount == 1 && lastObj == 1 && lastState == 3);
    CHECK(table.currentSwitch(1, 7) == 3);

    // Re-setting the SAME state still notifies (Sec10.2.2: "No curve, no
    // interpolation, no property application... this call is pure
    // bookkeeping" - no change-filter is described, unlike RTPC/State).
    table.setSwitch(1, 7, 3);
    CHECK(notifyCount == 2);
}

void testUW3Switch_GroupWatcherFiresForAnyObjectInGroup() {
    SwitchGroupTable table;
    std::vector<std::pair<GameObjectId, uint32_t>> seen;
    table.registerGroupWatcher(5, [&](GameObjectId obj, uint32_t state) { seen.emplace_back(obj, state); });

    table.setSwitch(100, 5, 1);  // different OBJECT, same group - Sec10.2.2 step3: filtered by group only
    table.setSwitch(200, 5, 2);
    table.setSwitch(100, 9, 99);  // different group - must NOT fire this watcher

    CHECK(seen.size() == 2);
    CHECK(seen[0].first == 100 && seen[0].second == 1);
    CHECK(seen[1].first == 200 && seen[1].second == 2);
}

void testUW3Switch_ExactWatcherIsStandaloneAndDoesNotStore() {
    SwitchGroupTable table;
    int exactFired = 0;
    uint32_t exactState = 0;
    table.registerExactWatcher(/*object*/ 42, /*group*/ 5, [&](uint32_t state) {
        ++exactFired;
        exactState = state;
    });

    // 0x00f542c0 (Sec10.2.2 step4): a standalone "just notify" primitive -
    // does not touch storage.
    table.notifyExactOnly(42, 5, 77);
    CHECK(exactFired == 1 && exactState == 77);
    CHECK(!table.hasSwitch(42, 5));  // storage untouched

    // A real setSwitch() on a DIFFERENT object in the same group must not
    // fire the exact watcher (filtered by group+object together).
    table.setSwitch(43, 5, 1);
    CHECK(exactFired == 1);
}

// ---------------------------------------------------------------------
// Unit U-W3 (Sec10.2.3): State - a separate manager; the id-only layer is
// derivable from the public call alone, the 4-property ramp layer needs an
// explicit authored value-set (never invented from bank data this library
// doesn't have).
// ---------------------------------------------------------------------
void testUW3State_IdOnlyLayerAndOpenBeforeSet() {
    StateGroupEngine engine;
    CHECK(throwsOpenStateError([&] { engine.currentStateId(11); }));
    engine.setState(11, 0xbeefu);
    CHECK(engine.currentStateId(11) == 0xbeefu);
}

void testUW3State_FirstApplyEstablishesBaselineNoRamp() {
    StateGroupEngine engine;
    CHECK(throwsOpenStateError([&] { engine.currentValue(11, 0); }));
    std::array<float, StateGroupEngine::kPropertyCount> initial = {1.0f, 2.0f, 3.0f, 4.0f};
    engine.applyProperties(11, 1, initial);  // default policy: AlwaysImmediate
    for (size_t i = 0; i < StateGroupEngine::kPropertyCount; ++i) {
        CHECK(feq(engine.currentValue(11, i), initial[i]));
        CHECK(!engine.isRamping(11, i));
    }
    CHECK(engine.currentStateId(11) == 1u);
}

void testUW3State_UnchangedPropertySkipsIdenticalExactCompare() {
    StateGroupEngine engine;
    std::array<float, StateGroupEngine::kPropertyCount> v1 = {1.0f, 2.0f, 3.0f, 4.0f};
    engine.applyProperties(11, 1, v1);
    engine.applyProperties(11, 2, v1);  // every property identical (exact equality, Sec10.2.3 step3)
    for (size_t i = 0; i < StateGroupEngine::kPropertyCount; ++i) CHECK(!engine.isRamping(11, i));
    CHECK(engine.currentStateId(11) == 2u);  // id still updates even when no property changed
}

void testUW3State_DefaultPolicySnapsImmediately() {
    StateGroupEngine engine;
    std::array<float, StateGroupEngine::kPropertyCount> v1 = {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, StateGroupEngine::kPropertyCount> v2 = {5.0f, 1.0f, 1.0f, 1.0f};
    engine.applyProperties(11, 1, v1);
    engine.applyProperties(11, 2, v2);  // property 0 changed; AlwaysImmediate -> snap, no ramp
    CHECK(!engine.isRamping(11, 0));
    CHECK(feq(engine.currentValue(11, 0), 5.0f));
}

void testUW3State_RampingViaPolicyAndCompleteRampHook() {
    class RampProperty0 : public StateGroupEngine::TransitionPolicy {
    public:
        bool shouldRamp(uint32_t, size_t propertyIndex, float, float) const override { return propertyIndex == 0; }
    };
    StateGroupEngine engine;
    RampProperty0 policy;
    std::array<float, StateGroupEngine::kPropertyCount> v1 = {1.0f, 10.0f, 10.0f, 10.0f};
    std::array<float, StateGroupEngine::kPropertyCount> v2 = {9.0f, 20.0f, 10.0f, 10.0f};  // props 0 and 1 change
    engine.applyProperties(11, 1, v1);
    engine.applyProperties(11, 2, v2, policy);

    CHECK(engine.isRamping(11, 0));                   // ramped: value NOT yet applied
    CHECK(feq(engine.currentValue(11, 0), 1.0f));      // still the OLD value until completeRamp()
    CHECK(!engine.isRamping(11, 1));                   // policy said snap for index 1
    CHECK(feq(engine.currentValue(11, 1), 20.0f));     // applied immediately
    CHECK(feq(engine.currentValue(11, 2), 10.0f));     // unchanged property untouched
    CHECK(engine.currentStateId(11) == 2u);

    // Test/harness hook (same convention as fireEventCallback/
    // completeBankLoad) - this library never advances a ramp on its own.
    engine.completeRamp(11, 0);
    CHECK(!engine.isRamping(11, 0));
    CHECK(feq(engine.currentValue(11, 0), 9.0f));
}

// ---------------------------------------------------------------------
// setSwitch()/setState() on NullWwiseBackend now go through the real
// internal Switch/State machinery above, not a bare log-and-return stub.
// ---------------------------------------------------------------------
void testUW3_NullBackendWiresSwitchAndStateInternally() {
    NullWwiseBackend backend;
    CHECK(backend.setSwitch(5, 3, /*object*/ 1));
    CHECK(backend.switchTable().currentSwitch(1, 5) == 3u);

    CHECK(backend.setState(8, 2));
    CHECK(backend.stateEngine().currentStateId(8) == 2u);
}

// ---------------------------------------------------------------------
// Unit U-W4a (Sec11): the RIFF/WAVE container both Vorbis source classes
// parse in front of their own bitstream work (Sec11.7.1), and the CONFIRMED
// Bank-vs-File split in how each gets its compressed bytes (Sec11.7.2).
// ---------------------------------------------------------------------
namespace {
// Builds a minimal synthetic "RIFF"/"WAVE" buffer with "fmt "/"cue "/
// "data"/"LIST" sub-chunks - small, synthetic sizes, never asserted to be a
// real/playable Vorbis file; only the CONFIRMED chunk-tag recognition is
// under test.
std::vector<uint8_t> buildSyntheticRiffWave() {
    std::vector<uint8_t> buf;
    auto putTag = [&](const char* fourcc) { for (int i = 0; i < 4; ++i) buf.push_back(static_cast<uint8_t>(fourcc[i])); };
    auto putU32 = [&](uint32_t v) {
        buf.push_back(static_cast<uint8_t>(v & 0xff));
        buf.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
        buf.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
        buf.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
    };
    putTag("RIFF");
    putU32(0);  // overall size, not checked by the parser
    putTag("WAVE");

    putTag("fmt ");
    putU32(4);
    buf.insert(buf.end(), {1, 2, 3, 4});

    putTag("cue ");
    putU32(2);
    buf.insert(buf.end(), {9, 9});

    putTag("LIST");
    putU32(4);
    buf.insert(buf.end(), {7, 7, 7, 7});

    putTag("data");
    putU32(6);
    buf.insert(buf.end(), {1, 2, 3, 4, 5, 6});

    return buf;
}
}  // namespace

void testUW4aVorbis_RiffWaveChunkWalkerFindsAllFourChunks() {
    std::vector<uint8_t> buf = buildSyntheticRiffWave();
    VorbisContainerChunks chunks = parseRiffWaveContainer(buf.data(), buf.size());
    CHECK(chunks.valid);
    CHECK(chunks.hasFmt && chunks.fmtSize == 4);
    CHECK(chunks.hasCue && chunks.cueSize == 2);
    CHECK(chunks.hasData && chunks.dataSize == 6);
    CHECK(chunks.hasList && chunks.listSize == 4);
}

void testUW4aVorbis_RejectsNonRiffWave() {
    std::vector<uint8_t> garbage = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    VorbisContainerChunks chunks = parseRiffWaveContainer(garbage.data(), garbage.size());
    CHECK(!chunks.valid);
    CHECK(chunks.rejectCode == 7);  // Sec11.7.1: "early-exit... returning result 7"
}

void testUW4aVorbis_BankSourceParsesResidentBlockAndDecodeConfigIsOpen() {
    std::vector<uint8_t> buf = buildSyntheticRiffWave();
    VorbisBankSource bank(buf.data(), buf.size());
    CHECK(bank.header().hasFmt && bank.header().hasData);
    // Sec11.9 (unit U-W4b): slot 10 is now a real, CONFIRMED decode
    // trigger - but with no output-block-count/decode-worker configured
    // yet, it throws naming that gap rather than inventing either.
    CHECK(throwsOpenStateError([&] { bank.decodeNextBlock(nullptr, 1024); }, "U-W4b"));
}

// ---------------------------------------------------------------------
// Unit U-W4b (Sec11.9, spec sync commit 6637644): the real decode-trigger
// slot (vtable slot 10 on both classes) and its seam, VorbisDecodeLoopWorker
// - the Vorbis-bitstream math itself and the Wwise-specific packet-
// framing/codebook-selection layer are explicitly NOT implemented here
// (still Team A's to spec / planned to come from libvorbis later); this
// only proves the TRIGGER CONTRACT (buffer allocation sized
// count*2048, output-struct fields, playback-position advance) works once
// something satisfies the seam.
// ---------------------------------------------------------------------
namespace {
class FixedSampleCountWorker : public VorbisDecodeLoopWorker {
public:
    explicit FixedSampleCountWorker(uint16_t samplesPerCall, uint32_t status = vorbis_decode_status::kFinalize)
        : samplesPerCall_(samplesPerCall), status_(status) {}
    uint32_t decode(void* /*decodeState*/, uint16_t /*formatWord*/, uint32_t /*requestedSamples*/,
                    uint8_t* outputBuffer, uint32_t outputBufferCapacity, uint16_t& decodedSampleCountOut) override {
        ++calls;
        decodedSampleCountOut = samplesPerCall_;
        // Sec11.9.2: "each produced sample advances the destination
        // pointer by exactly 2 bytes" - write a recognisable byte pattern
        // so the test can confirm the real output buffer was touched.
        uint32_t bytes = std::min<uint32_t>(static_cast<uint32_t>(samplesPerCall_) * 2, outputBufferCapacity);
        for (uint32_t i = 0; i < bytes; ++i) outputBuffer[i] = 0xAB;
        return status_;
    }
    int calls = 0;

private:
    uint16_t samplesPerCall_;
    uint32_t status_;
};
}  // namespace

void testUW4bVorbis_DecodeTriggerSeamProducesPcmViaInjectedWorker() {
    std::vector<uint8_t> buf = buildSyntheticRiffWave();
    VorbisBankSource bank(buf.data(), buf.size());
    auto worker = std::make_shared<FixedSampleCountWorker>(/*samplesPerCall*/ static_cast<uint16_t>(10));
    bank.decodeTrigger().setOutputBlockCount(1);  // 1 block = 2048 bytes (Sec11.9.1.1: count<<0xb)
    bank.decodeTrigger().setDecodeLoopWorker(worker);

    VorbisDecodeOutput out = bank.decodeNextBlock(/*decodeState*/ nullptr, /*requestedSamples*/ 10);
    CHECK(out.bufferPointer != nullptr);
    CHECK(out.bufferCapacity == 2048u);                               // 1 << 0xb
    CHECK(out.formatWord == kVorbisDecodeFormatWordBank);             // default (0x400)
    CHECK(out.decodedSampleCount == 10);
    CHECK(out.statusCode == vorbis_decode_status::kFinalize);
    CHECK(out.bufferPointer[0] == 0xAB);
    CHECK(worker->calls == 1);
    CHECK(bank.decodeTrigger().playbackSamplePosition() == 10u);      // Sec11.9.1.2: "add [esi+0xdc], eax"

    bank.decodeNextBlock(nullptr, 10);  // buffer reused, not reallocated (Sec11.9.1.1: "skip straight to part two")
    CHECK(bank.decodeTrigger().playbackSamplePosition() == 20u);
    CHECK(worker->calls == 2);
}

void testUW4bVorbis_DecodeTriggerMissingWorkerIsOpenNotInvented() {
    VorbisDecodeTrigger trigger;
    trigger.setOutputBlockCount(1);  // block count known, but no worker installed
    CHECK(throwsOpenStateError([&] { trigger.decodeNextBlock(nullptr, 10); }, "U-W4b"));
}

void testUW4aVorbis_FileSourcePullsGrowsLookaheadAndSeekResets() {
    std::vector<uint8_t> backing = buildSyntheticRiffWave();
    int pullCalls = 0;
    VorbisFileSource file([&](uint8_t* dst, size_t want, uint64_t streamOffset) -> size_t {
        ++pullCalls;
        size_t avail = streamOffset < backing.size() ? backing.size() - static_cast<size_t>(streamOffset) : 0;
        size_t n = std::min(want, avail);
        for (size_t i = 0; i < n; ++i) dst[i] = backing[static_cast<size_t>(streamOffset) + i];
        return n;
    });

    CHECK(file.header().hasFmt && file.header().hasData);
    CHECK(pullCalls == 1);
    CHECK(file.lookaheadValid() == backing.size());

    file.seek(5);  // Sec11.8 HIGH: "flush lookahead, reissue a read at the new offset"
    CHECK(file.lookaheadValid() == 0);
    CHECK(file.streamOffset() == 5u);
    file.fillLookahead(8);
    CHECK(pullCalls == 2);
    CHECK(file.streamOffset() == 5u);

    // Sec11.9: File's decode trigger is the same seam as Bank's (Sec11.9.1:
    // "same two-part shape") - unconfigured, it throws naming the same gap.
    CHECK(throwsOpenStateError([&] { file.decodeNextBlock(nullptr, 10); }, "U-W4b"));
}

// ---------------------------------------------------------------------
// Unit U-W5 (Sec6): the real per-block I/O scheduling loop's CONFIRMED
// structural shape - kConcurrentTransferSlots in flight at once (the two
// persistent 16 KiB buffer slots), FIFO queueing past that, one-tick
// completion latency (this project's own documented choice, not a
// CONFIRMED timing fact).
// ---------------------------------------------------------------------
void testUW5IoScheduler_ConcurrencyCapAndFifoCompletionOrder() {
    IoRequestScheduler sched;
    uint32_t idA = sched.requestRead(/*cookie*/ 0xA);
    uint32_t idB = sched.requestRead(/*cookie*/ 0xB);
    uint32_t idC = sched.requestRead(/*cookie*/ 0xC);
    CHECK(sched.pendingCount() == 3 && sched.inFlightCount() == 0);

    std::vector<IoRequestScheduler::CompletionInfo> completions;
    auto sink = [&](const IoRequestScheduler::CompletionInfo& info) { completions.push_back(info); };

    sched.tick(sink);  // A,B move into the two transfer slots; nothing completes yet
    CHECK(completions.empty());
    CHECK(sched.inFlightCount() == 2 && sched.pendingCount() == 1);

    sched.tick(sink);  // A,B complete (one-tick latency); C fills the freed slot
    CHECK(completions.size() == 2);
    CHECK(completions[0].pendingId == idA && completions[0].transferCookie == 0xAu);
    CHECK(completions[1].pendingId == idB && completions[1].transferCookie == 0xBu);
    CHECK(sched.inFlightCount() == 1 && sched.pendingCount() == 0);

    completions.clear();
    sched.tick(sink);  // C completes
    CHECK(completions.size() == 1);
    CHECK(completions[0].pendingId == idC && completions[0].transferCookie == 0xCu);
    CHECK(sched.inFlightCount() == 0 && sched.pendingCount() == 0);
}

}  // namespace

int main() {
    const auto run = [](const char* name, void (*fn)()) {
        try {
            fn();
        } catch (const std::exception& e) {
            std::cerr << name << ": unexpected exception: " << e.what() << "\n";
            ++g_failures;
        }
    };
    run("testBehaviour1_OrientationCorruption", testBehaviour1_OrientationCorruption);
    run("testBehaviour2_MultiPositionGuardAt16", testBehaviour2_MultiPositionGuardAt16);
    run("testBehaviour3_VoiceLineNoEngineGuardAndPostedFlag", testBehaviour3_VoiceLineNoEngineGuardAndPostedFlag);
    run("testBehaviour4_TeardownDoesNotStopEvents", testBehaviour4_TeardownDoesNotStopEvents);
    run("testBehaviour5_PauseResumeEventWide", testBehaviour5_PauseResumeEventWide);
    run("testBehaviour6_SmoothedRtpcDecayAndSnap", testBehaviour6_SmoothedRtpcDecayAndSnap);
    run("testBehaviour7_GlobalRtpcCacheDrops", testBehaviour7_GlobalRtpcCacheDrops);
    run("testBehaviour8_ListenerArmCounterGating", testBehaviour8_ListenerArmCounterGating);
    run("testBehaviour9_TimeGatedPositionalAndObstructionSlew", testBehaviour9_TimeGatedPositionalAndObstructionSlew);
    run("testBehaviour10a_SwitchChangeSkipsRecordsThisTick", testBehaviour10a_SwitchChangeSkipsRecordsThisTick);
    run("testBehaviour10b_LazyRegisterSkipsTick", testBehaviour10b_LazyRegisterSkipsTick);
    run("testBehaviour10c_NewRecordPlayingOnlyAfterDurationCrossesQueue",
        testBehaviour10c_NewRecordPlayingOnlyAfterDurationCrossesQueue);
    run("testBehaviour11_MessagePoolExhaustion", testBehaviour11_MessagePoolExhaustion);
    run("testBehaviour12_BankCallbackReportsBankIdNotCookie", testBehaviour12_BankCallbackReportsBankIdNotCookie);
    run("testBehaviour13_InitNoRollback", testBehaviour13_InitNoRollback);
    run("testBehaviour14_CodecVorbisFileIdAddressing", testBehaviour14_CodecVorbisFileIdAddressing);
    run("testPlayRecordFullLifecycle", testPlayRecordFullLifecycle);
    run("testStopOnFailedRecordDoesNotLeak", testStopOnFailedRecordDoesNotLeak);
    run("testConversationPool_AcquireReleaseExhaustionAndForceStopCascade",
        testConversationPool_AcquireReleaseExhaustionAndForceStopCascade);
    run("testInitShutdownOrder", testInitShutdownOrder);

    // Unit U-W3 (Sec10.2): Switch/State internal apply.
    run("testUW3Switch_AlwaysNotifiesNoChangeFilter", testUW3Switch_AlwaysNotifiesNoChangeFilter);
    run("testUW3Switch_GroupWatcherFiresForAnyObjectInGroup", testUW3Switch_GroupWatcherFiresForAnyObjectInGroup);
    run("testUW3Switch_ExactWatcherIsStandaloneAndDoesNotStore", testUW3Switch_ExactWatcherIsStandaloneAndDoesNotStore);
    run("testUW3State_IdOnlyLayerAndOpenBeforeSet", testUW3State_IdOnlyLayerAndOpenBeforeSet);
    run("testUW3State_FirstApplyEstablishesBaselineNoRamp", testUW3State_FirstApplyEstablishesBaselineNoRamp);
    run("testUW3State_UnchangedPropertySkipsIdenticalExactCompare",
        testUW3State_UnchangedPropertySkipsIdenticalExactCompare);
    run("testUW3State_DefaultPolicySnapsImmediately", testUW3State_DefaultPolicySnapsImmediately);
    run("testUW3State_RampingViaPolicyAndCompleteRampHook", testUW3State_RampingViaPolicyAndCompleteRampHook);
    run("testUW3_NullBackendWiresSwitchAndStateInternally", testUW3_NullBackendWiresSwitchAndStateInternally);

    // Unit U-W4a/U-W4b (Sec11/Sec11.9): Vorbis container + decode trigger.
    run("testUW4aVorbis_RiffWaveChunkWalkerFindsAllFourChunks", testUW4aVorbis_RiffWaveChunkWalkerFindsAllFourChunks);
    run("testUW4aVorbis_RejectsNonRiffWave", testUW4aVorbis_RejectsNonRiffWave);
    run("testUW4aVorbis_BankSourceParsesResidentBlockAndDecodeConfigIsOpen",
        testUW4aVorbis_BankSourceParsesResidentBlockAndDecodeConfigIsOpen);
    run("testUW4aVorbis_FileSourcePullsGrowsLookaheadAndSeekResets",
        testUW4aVorbis_FileSourcePullsGrowsLookaheadAndSeekResets);
    run("testUW4bVorbis_DecodeTriggerSeamProducesPcmViaInjectedWorker",
        testUW4bVorbis_DecodeTriggerSeamProducesPcmViaInjectedWorker);
    run("testUW4bVorbis_DecodeTriggerMissingWorkerIsOpenNotInvented",
        testUW4bVorbis_DecodeTriggerMissingWorkerIsOpenNotInvented);

    // Unit U-W5 (Sec6): the real I/O scheduling loop's structural shape.
    run("testUW5IoScheduler_ConcurrencyCapAndFifoCompletionOrder",
        testUW5IoScheduler_ConcurrencyCapAndFifoCompletionOrder);

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_audio_runtime_test: all checks passed\n";
    return 0;
}
