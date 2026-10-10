#pragma once

// sr3audioruntime - AudioRuntime: the GAME side of the Wwise adapter
// contract (spec-audio-runtime.md). Implements:
//   Sec1   the 25 calling-function crossings, via WwiseBackend
//   Sec2   the play record / voice record layouts and their state machine
//   Sec3   init/shutdown call order, the memory-hook/bank-callback shape
//   Sec4   the callback/feedback path (message queue, status -> state)
//   Sec5   all 14 behaviours a replacement must reproduce or knowingly fix
// against an injected WwiseBackend (null-backed in every test here).
//
// Voice-API scope note (Sec6 OPEN items 1/2): the voice API that writes
// voice positions/obstruction target/aux sends/per-voice RTPC/persona
// keys/marker callback, and that raises play-record request bits, is a
// LATER, out-of-scope unit. The setters below (createVoice, postPlayRecord,
// requestStop/Pause/Resume/Seek/Kill, and Voice's public fields) let a
// harness or test play that unit's role so the CONFIRMED machinery here -
// the state machine, the tick order, the callback path - can be exercised.
// They do not claim to BE the voice API.
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sr3audioruntime/global_rtpc.h"
#include "sr3audioruntime/play_record.h"
#include "sr3audioruntime/types.h"
#include "sr3audioruntime/voice.h"
#include "sr3audioruntime/wwise_backend.h"
#include "sr3luahost/open_state.h"

namespace sr3audioruntime {

// Sec3.1 step 3's config block - only the fields this library interprets or
// must validate. Everything else the real config carries (+0x00/+0x04/+0x08
// path strings, +0x14, +0x24, +0x28..+0x38, +0x4c..+0x5c, +0x64, +0x68) is
// uninterpreted and not modelled at all, same as the unread voice +0x20
// field (Sec2.2).
struct AudioLibraryConfig {
    uint32_t ioMemorySize = 0x10000;          // +0x18 -> stream-device I/O memory size
    uint32_t defaultPoolSize = 0x100000;      // +0x1c -> sound-engine default pool size
    uint32_t lowerEnginePoolSize = 0x100000;  // +0x20 -> lower-engine pool size
    // +0x3c/+0x40/+0x44/+0x48: identity is Sec6 OPEN item 6; the RULE
    // ("each must be non-zero or init fails") is CONFIRMED and IS enforced.
    uint32_t requiredField3c = 1, requiredField40 = 1, requiredField44 = 1, requiredField48 = 1;
    bool auxSendsEnabled = false;  // +0x69 -> 0x03172883 (Sec1.1 aux-send gate)
    uint64_t windowHandle = 0;     // +0x6c -> 0x03172970, passed to Wwise platform settings
};

// One queued status message (Sec4.2's 0x54-byte body, trimmed to the
// fields this library actually consumes).
struct StatusMessage {
    int32_t code = 0;
    uint32_t cookie = 0;      // play-record cookie (codes 4-9) or bank id (codes 0-3, behaviour #12)
    std::string markerLabel;  // code 5 only
};

// Minimal bank record: only the resident/idle/error bits Sec3.7 actually
// edits. The bank record's OWN 1-8 processing-state machine belongs to
// spec-audio-format.md Sec7.5/Sec8, out of this file's scope.
struct BankRecord {
    uint32_t bankId = 0;
    bool resident = false;   // +0x54 bit 3 (HIGH)
    bool idle = true;        // +0x54 bit 0 (HIGH)
    bool errorFlag = false;  // +0x5b bit 1
    // Sec3.7's "only if bit 2 is set" clause for a load failure (a
    // re-request already pending when the failure callback lands); Sec6
    // OPEN item 10 leaves whether 0x004673c0 covers more than this. Default
    // false (no re-request pending), matching "the state machine cleared
    // bit 2 just before LoadBank".
    bool reRequestPendingAtCallback = false;
    uint32_t wwiseLoadCookie = 0;  // the record-address stand-in handed to LoadBank (behaviour #12)
};

class AudioRuntime {
public:
    static constexpr size_t kDefaultMessagePoolCapacity = 1000;  // Sec4.2: 1000 items

    explicit AudioRuntime(WwiseBackend& backend, size_t messagePoolCapacity = kDefaultMessagePoolCapacity)
        : backend_(backend), messagePoolCapacity_(messagePoolCapacity) {}

    // ---------------------------------------------------------------
    // Sec3.1 / Sec3.2: init, Sec3.4: shutdown. Every step appends to
    // stepLog() in CONFIRMED order; WwiseBackend's own callLog() carries
    // the Sec3.2 sub-steps. No rollback on failure (behaviour #13): a
    // failure part-way leaves earlier steps' effects (and earlier
    // WwiseBackend state) exactly as they were.
    // ---------------------------------------------------------------
    bool init(const AudioLibraryConfig& config);
    void shutdown();
    bool engineUp() const { return engineUp_; }
    const std::vector<std::string>& stepLog() const { return stepLog_; }

    // ---------------------------------------------------------------
    // Minimal voice lifecycle (Sec1.1 0x0045da50's documented side effect:
    // synchronous RegisterGameObj; 0x0045cc10's teardown).
    // ---------------------------------------------------------------
    GameObjectId createVoice(const std::string& debugName);
    // 0x0045cc10 (Sec1.1 last row / behaviour #4): frees every play record
    // WITHOUT issuing StopPlayingID for any of them, then unregisters and
    // frees the voice.
    void destroyVoiceNow(GameObjectId voice);
    Voice* findVoice(GameObjectId id);
    const Voice* findVoice(GameObjectId id) const;

    // ---------------------------------------------------------------
    // Play-record creation and request-bit setters (voice API surface,
    // Sec6 OPEN items 1/2 - see the header's scope note). `cookie` is
    // caller-chosen and must be unique across all live records (mirrors
    // the real handle space, Sec2.1).
    // ---------------------------------------------------------------
    bool postPlayRecord(GameObjectId voice, uint32_t cookie, uint32_t eventIdOrLineKey, bool isVoiceLine,
                         int32_t startDelayMs = 0);
    void requestStop(GameObjectId voice, uint32_t cookie);
    void requestPause(GameObjectId voice, uint32_t cookie);
    void requestResume(GameObjectId voice, uint32_t cookie);
    void requestSeek(GameObjectId voice, uint32_t cookie, int32_t positionMs);
    void requestKill(GameObjectId voice, uint32_t cookie);  // bit 0x800000
    PlayRecord* findPlayRecord(GameObjectId voice, uint32_t cookie);

    // ---------------------------------------------------------------
    // The `.ctdg_pc` active-conversation pool (spec-conversation-format.md
    // Sec4-Sec6; spec-audio-runtime.md Sec1.2a / Sec2.1 +0x1c - CORRECTED
    // 2026-10-08, see play_record.h's header comment: this project
    // previously mis-modelled these three slots as "persona-line (.lm_pc)
    // streaming slots"; .lm_pc is per-soundbank line-variant/weight
    // metadata, not streamed audio, and has no play-record-level slot at
    // all). Real base `0x0322d828`, stride `0x58`, exactly
    // kConversationPoolSize (3) entries - confirmed by five independent
    // loop bounds. The real Lua entry point is `audio_conversation_play`,
    // whose chain (`0x006dec90` -> `0x00469f90` -> `0x0046a510`) ends at
    // the allocate this project's postConversationTurn() below reproduces.
    // ---------------------------------------------------------------

    // Acquires a free slot (0..kConversationPoolSize-1), or
    // kNoConversationSlot when all kConversationPoolSize are already busy.
    // EXHAUSTION BEHAVIOUR: spec-conversation-format.md Sec5's own OPEN
    // item is the validation-LADDER ORDER, not what happens on a full
    // pool specifically, and no spec text anywhere describes an
    // eviction/queueing/overflow policy for it - so this project does not
    // invent one. This reproduces only the one shape every OTHER
    // fixed-size pool already documented in this codebase uses on
    // exhaustion: allocate-or-fail, nothing queued, nothing silently
    // substituted (the 1000-item message pool, Sec4.2 behaviour #11; the
    // engine's own 330-slot `.ctdg_pc` STASH registry, a different pool
    // from this one, spec-conversation-format.md Sec1: "scans ... for
    // that key ... and on a free slot stores").
    uint8_t acquireConversationSlot();

    // Frees `slot` (no-op if already free or kNoConversationSlot). Per
    // Sec1.2a, releasing a slot force-stops every play record - on every
    // voice - still carrying this slot before freeing the entry: this
    // raises each such record's stop request bit (the same mechanism
    // requestStop() uses), since this project's state machine has no
    // separate synchronous "force" path - a stopped record is processed
    // on its own next tick like any other stop request, same as every
    // other request-bit-driven transition here.
    void releaseConversationSlot(uint8_t slot);

    bool conversationSlotBusy(uint8_t slot) const;

    // Posts a scripted-conversation voice line - the real consumer's
    // terminal effect (spec-conversation-format.md Sec5's `FUN_0046ac60`
    // cluster; spec-audio-runtime.md Sec1.2a's allocate/populate/release
    // cluster and the Lua call chain ending at `0x0046a510`): acquires a
    // pool slot, tags the resulting play record with it
    // (PlayRecord::conversationSlot, +0x1c), and posts it exactly as
    // postPlayRecord(voice, cookie, lineKey, /*isVoiceLine=*/true) would.
    // Returns false WITHOUT posting anything, and without creating a play
    // record, when the pool is exhausted (see acquireConversationSlot) or
    // when postPlayRecord itself would refuse (unknown voice, or a
    // duplicate cookie) - in the latter case the acquired slot is
    // released again so a rejected post never leaks one.
    bool postConversationTurn(GameObjectId voice, uint32_t cookie, uint32_t lineKey);

    // ---------------------------------------------------------------
    // Ticking (Sec2.4, Sec1.2, Sec1.3). The simulated millisecond clock
    // drives both the voice positional-update deadline and a play
    // record's start-delay deadline (Sec2.1 +0x10).
    // ---------------------------------------------------------------
    void advanceTimeMs(int64_t ms) { nowMs_ += ms; }
    int64_t nowMs() const { return nowMs_; }

    // 0x0045d1e0 on every live voice, Sec2.4's 6-step order. Also runs the
    // global "clear every voice's sends once" pass (Sec1.1 0x0045cd80
    // continuation) when requestClearAllAuxSends() was called since the
    // last pass.
    void tickAllVoices();
    void requestClearAllAuxSends() { clearAllAuxSendsRequested_ = true; }

    // 0x0045f690: the global frame tick (smoothed/mix/single-value RTPCs,
    // listener, RenderAudio).
    void runFrameTick();

    // Sec4.5's odd/even cadence: the real Main Audio Thread runs the voice
    // tick and the frame tick only on every second pass. This convenience
    // wrapper reproduces that cadence (increments the pass counter twice
    // per call, as the real main loop does); tests wanting fine-grained
    // control over individual behaviours call tickAllVoices()/
    // runFrameTick() directly instead.
    void runMainAudioThreadPass();

    // ---------------------------------------------------------------
    // Sec4.2/Sec4.3: the message queue and its drain loop. Posting is
    // normally internal (PostEvent/StopPlayingID/etc. callbacks and the
    // bank callbacks all post through this); exposed for tests that want
    // to drive behaviour #11 (pool exhaustion) directly.
    // ---------------------------------------------------------------
    bool postStatusMessage(int32_t code, uint32_t cookie, const std::string& markerLabel = {});
    void pumpCallbackThread();  // drains and dispatches every queued message
    size_t queuedMessageCount() const { return messageQueue_.size(); }
    size_t droppedMessageCount() const { return droppedMessageCount_; }

    // ---------------------------------------------------------------
    // Banks (Sec1.4, Sec3.7, behaviour #12).
    // ---------------------------------------------------------------
    void registerBank(uint32_t bankId);  // test/harness convenience: creates the record
    bool requestLoadBank(uint32_t bankId, int32_t memoryPool = 0);
    bool requestUnloadBank(uint32_t bankId);
    const BankRecord* findBank(uint32_t bankId) const;
    void setBankReRequestPending(uint32_t bankId, bool pending);  // Sec3.7 "bit 2" scenario

    // ---------------------------------------------------------------
    // Globals (Sec1.3), exposed for direct inspection/seeding in tests.
    // ---------------------------------------------------------------
    GlobalRtpcCache& rtpcCache() { return rtpcCache_; }
    SmoothedGlobalRtpc& smoothedRtpc() { return smoothedRtpc_; }
    MixRtpcBank& mixRtpcs() { return mixRtpcs_; }
    SingleValueRtpc& singleValueRtpc() { return singleValueRtpc_; }
    ListenerState& listener() { return listener_; }

    // Sec1.1 0x004709b0 slot 0: "the global send 0x03172950/0x03172954"
    // used in place of a voice's own aux-send-0 when voice flag bit 0 is
    // clear. No spec text gives this an initial value (unlike the
    // smoothed-RTPC pair); defaulting its id to 0 is this project's
    // neutral "not configured yet" state (a zero id is already, by
    // Sec1.1's own rule, "not considered" - so leaving it unset simply
    // means slot 0 is never sent until a caller sets one, not an invented
    // value for it).
    AuxSend& globalAuxSend0() { return globalAuxSend0_; }

    // Sec6 OPEN item 5: the code-7 side call 0x0045c870(record) when byte
    // 0x03171a5a is set and the record never started - purpose unknown.
    // Disabled by default (matching "that byte clear"); enabling it and
    // hitting the condition throws OpenStateError, naming the item, rather
    // than inventing what the call does.
    void setCode7SideCallByteSet(bool set) { code7SideCallByteSet_ = set; }

    // Sec6 OPEN item 3: 0x0045c160's tiered per-voice-RTPC "changed" test.
    // Disabled (throws) by default; a test that wants to exercise the
    // positional/obstruction path on a voice with the per-voice RTPC
    // enabled (+0xa6) must supply one via this hook.
    using RtpcChangedTest = std::function<bool(float current, float lastSent)>;
    void setPerVoiceRtpcChangedTestForTesting(RtpcChangedTest fn) { perVoiceRtpcChangedTest_ = std::move(fn); }

private:
    bool perVoiceRtpcChanged(float current, float lastSent) const;
    void tickOneVoice(Voice& voice, bool& skipToNextVoice);
    void updateVoicePositional(Voice& voice);
    void flushVoiceRtpcList(Voice& voice);
    void flushVoiceSwitchList(Voice& voice);
    bool runPlayRecordStateMachine(Voice& voice, PlayRecord& record);
    void unlinkDeadRecords(Voice& voice);
    void teardownVoiceInternal(Voice& voice);

    void handleBankStatus(int32_t code, uint32_t bankId);
    void handlePlayRecordStatus(int32_t code, uint32_t cookie, const std::string& markerLabel);
    void handleVoiceLineEnded(uint32_t cookie);

    PostEventCallback makeSoundEffectCallback();
    PostEventCallback makeVoiceLineCallback();

    WwiseBackend& backend_;
    size_t messagePoolCapacity_;

    bool engineUp_ = false;  // 0x031728c5 / 0x031728c6, modelled as one bool (they move together)
    bool mainAudioThreadStarted_ = false;
    bool callbackThreadStarted_ = false;
    bool auxSendsEnabled_ = false;
    int32_t channelCount_ = 2;

    std::vector<std::string> stepLog_;
    int64_t nowMs_ = 0;
    uint32_t mainLoopPassCounter_ = 0;

    GameObjectId nextVoiceId_ = 1;
    std::unordered_map<GameObjectId, Voice> voices_;
    // cookie -> owning voice, for the O(1) "resolve a status message's
    // cookie to a play record" lookup Sec2.1 describes (0x004613a0's hash
    // table, functionally). A cookie absent here is a "late callback for a
    // freed record", dropped harmlessly (Sec2.1).
    std::unordered_map<uint32_t, GameObjectId> cookieOwner_;

    bool clearAllAuxSendsRequested_ = false;

    std::deque<StatusMessage> messageQueue_;
    size_t droppedMessageCount_ = 0;

    std::unordered_map<uint32_t, BankRecord> banks_;
    uint32_t nextBankLoadCookie_ = 0x1000;

    // The `.ctdg_pc` active-conversation pool's occupancy (see the public
    // acquire/release/postConversationTurn methods above). true = busy.
    std::array<bool, kConversationPoolSize> conversationSlotBusy_{};

    GlobalRtpcCache rtpcCache_;
    SmoothedGlobalRtpc smoothedRtpc_;
    MixRtpcBank mixRtpcs_;
    SingleValueRtpc singleValueRtpc_;
    ListenerState listener_;

    bool code7SideCallByteSet_ = false;
    RtpcChangedTest perVoiceRtpcChangedTest_;
    AuxSend globalAuxSend0_;
};

}  // namespace sr3audioruntime
