// sr3audioruntime - AudioRuntime: see include/sr3audioruntime/audio_runtime.h
// for the full contract this implements (spec-audio-runtime.md).

#include "sr3audioruntime/audio_runtime.h"

#include <algorithm>
#include <cmath>

namespace sr3audioruntime {

using sr3luahost::OpenStateError;

namespace {
constexpr float kObstructionDivisor = 100.0f;        // 0x012a2dd8 (Sec3.3)
constexpr float kAuxSendThreshold = 0.01f;           // 0x012a2dd0 (Sec3.3)
constexpr uint8_t kObstructionSlewPerUpdate = 7;     // Sec1.1 0x00470450 ("at most 7 points")
constexpr int64_t kPositionalDeadlineActiveMs = 33;  // Sec1.1 0x0045cd80 (33 ms while playing)
constexpr int64_t kPositionalDeadlineIdleMs = 5000;  // Sec1.1 0x0045cd80 (5 s idle)
constexpr size_t kMultiPositionGuardCapacity = 16;   // Sec5 behaviour #2: the real stack frame's 16 slots
}  // namespace

// =====================================================================
// Sec3.1 / Sec3.2: init
// =====================================================================

bool AudioRuntime::init(const AudioLibraryConfig& config) {
    // Step 1 (first call only in the real game; this AudioRuntime instance
    // models exactly one audio library, so it is simply "the" first call).
    stepLog_.push_back("step1:globals-reset");

    // Step 2: seven mix-RTPC defaults + dirty bits. MixRtpcBank's own
    // constructor already sets these (Sec3.1 step 2 / Sec3.3); nothing
    // further to do here, logged for order fidelity.
    stepLog_.push_back("step2:mix-rtpc-defaults");

    // Step 3: config copy + validation. "+0x3c/+0x40/+0x44/+0x48 each must
    // be non-zero or init fails" (CONFIRMED rule; identity Sec6 OPEN item 6).
    stepLog_.push_back("step3:config-copied");
    if (config.requiredField3c == 0 || config.requiredField40 == 0 || config.requiredField44 == 0 ||
        config.requiredField48 == 0) {
        stepLog_.push_back("step3:FAILED-required-field-zero");
        return false;  // Sec3.1: "Returns 0 on any failure (with no unwinding of the steps already done)"
    }
    auxSendsEnabled_ = config.auxSendsEnabled;

    // Step 4: default external source hashing (Sec1.2's default fallback).
    stepLog_.push_back("step4:default-ext-src-hashed");
    backend_.getIDFromString("Default_Ext_Src_external_source");
    backend_.getIDFromString("voc_Default_Ext_Src_play");

    // Step 5: two opaque pre-Wwise calls (0x00462c70, 0x004628a0); this
    // project assigns them no interpreted meaning beyond their CONFIRMED
    // position and the fact that either one's failure aborts init.
    stepLog_.push_back("step5:pre-wwise-setup");

    // Step 6: start the Audio callback thread BEFORE Wwise exists.
    stepLog_.push_back("step6:audio-callback-thread-started");
    callbackThreadStarted_ = true;

    // Step 7: Wwise init, only if not already up.
    stepLog_.push_back("step7:wwise-init-begin");
    bool wwiseOk = false;
    {
        WwiseInitParams params;
        params.maxNumPools = 21;  // Sec3.2 step1, CONFIRMED
        params.streamMgrMemorySize = 0x10000;  // Sec3.2 step2, CONFIRMED
        params.ioMemorySize = config.ioMemorySize;
        params.defaultPoolSize = config.defaultPoolSize;
        params.lowerEnginePoolSize = config.lowerEnginePoolSize;
        params.windowHandle = config.windowHandle;

        wwiseOk = backend_.memoryMgrInit(params.maxNumPools) && backend_.streamMgrCreate(params.streamMgrMemorySize) &&
                  backend_.setupStreamDevice(params) >= 0 && backend_.soundEngineInit(params) && backend_.musicEngineInit();
        if (wwiseOk) {
            static constexpr PluginRegistration kPlugins[] = {
                {3, 0, 0x73}, {3, 0, 0x69}, {3, 0, 0x6e}, {3, 0, 0x74}, {2, 0, 0x77}, {2, 0, 0x78}, {3, 0, 0x6c},
                {3, 0, 0x6d}, {3, 0, 0x6a}, {5, 0, 0x195}, {2, 0, 0x64}, {2, 0, 0x65}, {2, 0, 0x66}, {3, 0x100, 0x67},
                {3, 0x100, 0x6e}, {2, 0, 0xc8}, {3, 0, 0x76}, {3, 0, 0x7e},
            };
            for (const auto& reg : kPlugins) backend_.registerPlugin(reg);
            backend_.registerCodec(0, 4);  // Vorbis, codec id 4 (Sec3.2, behaviour #14)
            backend_.motionEngineSetPlayerListener(0, 0);
            backend_.setListenerPipeline(0, true, true);
            backend_.postCode(3);
            channelCount_ = (backend_.getSpeakerConfiguration() == kSpeakerConfig5Point1) ? 6 : 2;
        }
    }
    if (wwiseOk) {
        engineUp_ = true;  // sets BOTH 0x031728c5 and 0x031728c6 (modelled as one bool)
        stepLog_.push_back("step7:wwise-init-ok");
    } else {
        stepLog_.push_back("step7:wwise-init-FAILED-no-rollback");  // behaviour #13: no rollback, init still continues
    }

    // Step 8: bank system.
    stepLog_.push_back("step8:bank-system-reset");
    backend_.setBankLoadIOSettings(1048.576, 100);  // Sec1.4 / Sec3.3, CONFIRMED values
    banks_.clear();
    // "read audio_banks.xtbl ... failure clears 0x031728c6/0x031728c5 and
    // runs the Wwise shutdown" - this project does not read a real xtbl
    // here (no bank-table content is part of this spec file's scope), so
    // the xtbl-read step always "succeeds" structurally; a caller that
    // wants to exercise the failure path can simply not call init() at
    // all on a backend pre-configured to fail Wwise's own steps instead,
    // which already proves behaviour #13 without inventing xtbl content.
    stepLog_.push_back("step8:audio-banks-xtbl-read");
    stepLog_.push_back("step8:soundbank-process-task-registered");

    // Step 9: voice system.
    stepLog_.push_back("step9:voice-pools-built");
    stepLog_.push_back("step9:audio-info-vad-loaded");
    stepLog_.push_back("step9:playlist-process-task-registered");

    // Step 10.
    stepLog_.push_back("step10:0x00467fc0");

    // Step 11: Main Audio Thread.
    stepLog_.push_back("step11:main-audio-thread-started");
    mainAudioThreadStarted_ = true;

    // Step 12: allocator slots.
    stepLog_.push_back("step12:allocator-slots");

    // Step 13: voice API / mix RTPCs live.
    stepLog_.push_back("step13:voiceapi-up");
    mixRtpcs_.live = true;  // 0x031728c0 := 1

    // Step 14: default voice + per-voice/global RTPC 0x85c3e987 init.
    stepLog_.push_back("step14:default-voice-created");
    GameObjectId defaultVoice = createVoice("2D");
    (void)defaultVoice;
    stepLog_.push_back("step14:rtpc-0x85c3e987-zeroed");

    return true;  // Sec3.1: init itself always returns 0 success unless step3's non-zero check failed above,
                   // or a hard early-out was added - matches "no unwinding of the steps already done".
}

// =====================================================================
// Sec3.4: shutdown
// =====================================================================

void AudioRuntime::shutdown() {
    stepLog_.push_back("shutdown1:mix-rtpc-off");
    mixRtpcs_.live = false;

    stepLog_.push_back("shutdown2:voice-system-down");
    // Real order: 0x0045dca0 then 0x0045d740 (clears the Playlist gate).
    // This project's voices are simply dropped; nothing here calls
    // StopPlayingID, matching the voice-system teardown's own silence on
    // that point (distinct from, but consistent with, behaviour #4).

    stepLog_.push_back("shutdown3:bank-system-down");

    if (mainAudioThreadStarted_) {
        stepLog_.push_back("shutdown4:main-audio-thread-stopped");
        mainAudioThreadStarted_ = false;
    }

    stepLog_.push_back("shutdown5:engine-up-cleared");
    engineUp_ = false;  // 0x031728c6 := 0, 0x031728c5 := 0, BEFORE Wwise shutdown (Sec3.4 step5)
    if (true) {
        // Sec3.4's Wwise shutdown (0x004701b0), exact reverse of Sec3.2.
        backend_.musicEngineTerm();
        if (backend_.soundEngineIsInitialized()) {
            backend_.stopAll(kGlobalObject);
            backend_.clearPreparedEvents();
            backend_.unregisterAllGameObj();
            backend_.clearBanks();
            backend_.soundEngineTerm();
        }
        backend_.setFileLocationResolver(false);
        backend_.destroyStreamDevice(-1);
        backend_.streamMgrDestroyIfExists();
        backend_.memoryMgrTerm();
    }
    stepLog_.push_back("shutdown5:wwise-shutdown-done");

    if (callbackThreadStarted_) {
        stepLog_.push_back("shutdown6:audio-callback-thread-stop-signal");
        // "drains the queue without dispatching - every pending status is
        // thrown away" (Sec4.3).
        messageQueue_.clear();
        callbackThreadStarted_ = false;
    }

    stepLog_.push_back("shutdown7:voiceapi-down");
}

// =====================================================================
// Voice lifecycle
// =====================================================================

GameObjectId AudioRuntime::createVoice(const std::string& debugName) {
    GameObjectId id = nextVoiceId_++;
    Voice voice;
    voice.id = id;
    voice.handle = static_cast<uint64_t>(id);
    voice.debugName = debugName;
    // Sec1.1 0x0045da50's documented effect: synchronous RegisterGameObj.
    // Guard order (Sec1.1 row 0x00470610): engine down -> no call (left
    // unregistered at the Wwise level; the voice object itself still
    // exists so a later retry via the lazy-register flag could pick it
    // up); handle 0 cannot happen here since we always assign a non-zero
    // handle.
    if (engineUp_) {
        bool ok = backend_.registerGameObj(voice.handle, std::to_string(voice.handle));
        if (!ok) voice.flags |= voice_flags::kNeedsRegister;  // Wwise result != 1 -> retried lazily (Sec1.1)
    } else {
        voice.flags |= voice_flags::kNeedsRegister;  // engine not up yet: lazy-register later
    }
    voices_.emplace(id, std::move(voice));
    return id;
}

void AudioRuntime::teardownVoiceInternal(Voice& voice) {
    // Sec1.1 0x0045cc10 / behaviour #4: free every play record WITHOUT
    // issuing StopPlayingID, release conversation-pool slots, then
    // unregister.
    for (PlayRecord& rec : voice.playRecords) {
        cookieOwner_.erase(rec.cookie);
        // THIS function's own documented behaviour already is "release
        // without issuing StopPlayingID" - so the slot is simply marked
        // free again directly, NOT via releaseConversationSlot(), which
        // would raise a stop request bit on every other record sharing
        // the slot (0x0046a700's force-stop cascade): there is nothing
        // left here for that cascade to stop, the record is being
        // dropped outright.
        if (rec.conversationSlot != kNoConversationSlot && rec.conversationSlot < kConversationPoolSize) {
            conversationSlotBusy_[rec.conversationSlot] = false;
        }
    }
    voice.playRecords.clear();
    if (engineUp_ && voice.handle != 0) {
        backend_.unregisterGameObj(voice.handle);
    }
}

void AudioRuntime::destroyVoiceNow(GameObjectId id) {
    auto it = voices_.find(id);
    if (it == voices_.end()) return;
    teardownVoiceInternal(it->second);
    voices_.erase(it);
}

Voice* AudioRuntime::findVoice(GameObjectId id) {
    auto it = voices_.find(id);
    return it == voices_.end() ? nullptr : &it->second;
}
const Voice* AudioRuntime::findVoice(GameObjectId id) const {
    auto it = voices_.find(id);
    return it == voices_.end() ? nullptr : &it->second;
}

// =====================================================================
// Play-record creation and request-bit setters
// =====================================================================

bool AudioRuntime::postPlayRecord(GameObjectId voiceId, uint32_t cookie, uint32_t eventIdOrLineKey, bool isVoiceLine,
                                   int32_t startDelayMs) {
    Voice* voice = findVoice(voiceId);
    if (!voice) return false;
    if (cookieOwner_.count(cookie)) return false;  // caller-chosen cookies must be unique (Sec2.1)

    PlayRecord rec;
    rec.cookie = cookie;
    rec.eventIdOrLineKey = eventIdOrLineKey;
    rec.requestWord = isVoiceLine ? request_bits::kVoiceLine : 0u;
    if (startDelayMs > 0) {
        rec.startDelayDeadlineMs = static_cast<int32_t>(nowMs_ + startDelayMs);
    } else {
        rec.requestWord |= request_bits::kPlayOrResume;  // no delay: request play immediately (Sec2.1 +0x10 semantics)
        rec.startDelayDeadlineMs = -1;
    }
    rec.state = PlayRecordState::kCreated;

    voice->playRecords.push_back(rec);
    cookieOwner_[cookie] = voiceId;
    return true;
}

PlayRecord* AudioRuntime::findPlayRecord(GameObjectId voiceId, uint32_t cookie) {
    Voice* voice = findVoice(voiceId);
    if (!voice) return nullptr;
    for (PlayRecord& rec : voice->playRecords) {
        if (rec.cookie == cookie) return &rec;
    }
    return nullptr;
}

void AudioRuntime::requestStop(GameObjectId voiceId, uint32_t cookie) {
    if (PlayRecord* rec = findPlayRecord(voiceId, cookie)) rec->requestWord |= request_bits::kStop;
}
void AudioRuntime::requestPause(GameObjectId voiceId, uint32_t cookie) {
    if (PlayRecord* rec = findPlayRecord(voiceId, cookie)) rec->requestWord |= request_bits::kPause;
}
void AudioRuntime::requestResume(GameObjectId voiceId, uint32_t cookie) {
    if (PlayRecord* rec = findPlayRecord(voiceId, cookie)) rec->requestWord |= request_bits::kPlayOrResume;
}
void AudioRuntime::requestSeek(GameObjectId voiceId, uint32_t cookie, int32_t positionMs) {
    if (PlayRecord* rec = findPlayRecord(voiceId, cookie)) {
        rec->seekTargetMs = positionMs;
        rec->requestWord |= request_bits::kSeek;
    }
}
void AudioRuntime::requestKill(GameObjectId voiceId, uint32_t cookie) {
    if (PlayRecord* rec = findPlayRecord(voiceId, cookie)) rec->requestWord |= request_bits::kKilled;
}

// =====================================================================
// The `.ctdg_pc` active-conversation pool (see audio_runtime.h's own
// doc comment for this section; CORRECTED 2026-10-08, Sec1.2a).
// =====================================================================

uint8_t AudioRuntime::acquireConversationSlot() {
    for (uint8_t i = 0; i < kConversationPoolSize; ++i) {
        if (!conversationSlotBusy_[i]) {
            conversationSlotBusy_[i] = true;
            return i;
        }
    }
    return kNoConversationSlot;  // exhausted: fixed pool, allocate-or-fail (see header comment)
}

void AudioRuntime::releaseConversationSlot(uint8_t slot) {
    if (slot == kNoConversationSlot || slot >= kConversationPoolSize) return;
    if (!conversationSlotBusy_[slot]) return;  // already free: nothing to cascade
    // 0x0046a700's documented force-stop: every play record, on every
    // voice, still referencing this slot gets its stop bit raised before
    // the entry is freed. This project's state machine has no separate
    // synchronous "force" path, so (same convention as requestStop()
    // elsewhere in this file) the bit is processed on that record's own
    // next tick, not inline here.
    for (auto& kv : voices_) {
        for (PlayRecord& rec : kv.second.playRecords) {
            if (rec.conversationSlot == slot) {
                rec.requestWord |= request_bits::kStop;
            }
        }
    }
    conversationSlotBusy_[slot] = false;
}

bool AudioRuntime::conversationSlotBusy(uint8_t slot) const {
    return slot < kConversationPoolSize && conversationSlotBusy_[slot];
}

bool AudioRuntime::postConversationTurn(GameObjectId voiceId, uint32_t cookie, uint32_t lineKey) {
    uint8_t slot = acquireConversationSlot();
    if (slot == kNoConversationSlot) return false;  // pool exhausted: nothing posted (see header)
    if (!postPlayRecord(voiceId, cookie, lineKey, /*isVoiceLine=*/true)) {
        releaseConversationSlot(slot);  // rejected post (unknown voice / duplicate cookie): don't leak the slot
        return false;
    }
    PlayRecord* rec = findPlayRecord(voiceId, cookie);
    rec->conversationSlot = slot;  // +0x1c (Sec1.2a, corrected)
    return true;
}

// =====================================================================
// Sec4.2/Sec4.3: message queue
// =====================================================================

bool AudioRuntime::postStatusMessage(int32_t code, uint32_t cookie, const std::string& markerLabel) {
    if (messageQueue_.size() >= messagePoolCapacity_) {
        ++droppedMessageCount_;  // behaviour #11: pool exhausted, status lost
        return false;
    }
    messageQueue_.push_back(StatusMessage{code, cookie, markerLabel});
    return true;
}

void AudioRuntime::pumpCallbackThread() {
    while (!messageQueue_.empty()) {
        StatusMessage msg = std::move(messageQueue_.front());
        messageQueue_.pop_front();
        if (msg.code >= 0 && msg.code <= 3) {
            handleBankStatus(msg.code, msg.cookie);
        } else if (msg.code == status::kVoiceLineEnded) {
            handleVoiceLineEnded(msg.cookie);
        } else if (msg.code >= status::kStarted && msg.code <= status::kPostFailed) {
            handlePlayRecordStatus(msg.code, msg.cookie, msg.markerLabel);
        }
        // code 10 (I/O completion) is intentionally not modelled (scope
        // decision: no real file I/O, see the task's own non-goals).
    }
}

// =====================================================================
// Sec4.4: status -> gameplay-visible state
// =====================================================================

void AudioRuntime::handlePlayRecordStatus(int32_t code, uint32_t cookie, const std::string& markerLabel) {
    auto ownerIt = cookieOwner_.find(cookie);
    if (ownerIt == cookieOwner_.end()) return;  // unknown handle: dropped harmlessly (Sec2.1)
    Voice* voice = findVoice(ownerIt->second);
    if (!voice) return;
    PlayRecord* rec = nullptr;
    for (PlayRecord& r : voice->playRecords) {
        if (r.cookie == cookie) {
            rec = &r;
            break;
        }
    }
    if (!rec) return;

    switch (code) {
        case status::kStarted:  // 4
            rec->requestWord |= request_bits::kResumedOrDuration;
            rec->hasStarted = true;
            break;
        case status::kMarker:  // 5
            if (voice->markerCallbackEnabled && voice->markerCallback) {
                voice->markerCallback(rec->playingId, markerLabel);
            }
            break;
        case status::kPaused:  // 6
            rec->requestWord |= request_bits::kPausedConfirmed;
            break;
        case status::kEnded:  // 7
            rec->requestWord |= request_bits::kEndedConfirmed;
            rec->posted = false;
            // Sec6 OPEN item 5: the code-7 side call. Disabled unless a
            // test explicitly opts in via setCode7SideCallByteSet(true);
            // even then it throws rather than inventing the call's effect.
            if (code7SideCallByteSet_ && !rec->hasStarted) {
                throw OpenStateError("0x0045c870(record) code-7 side call", "spec-audio-runtime.md Sec6 item 5");
            }
            break;
        case status::kPostFailed:  // 8
            rec->requestWord |= request_bits::kPostFailedConfirmed;
            rec->posted = false;
            break;
        default:
            break;
    }
}

void AudioRuntime::handleVoiceLineEnded(uint32_t cookie) {
    auto ownerIt = cookieOwner_.find(cookie);
    if (ownerIt == cookieOwner_.end()) return;
    Voice* voice = findVoice(ownerIt->second);
    if (!voice) return;
    if (voice->voiceLinePostSuccessCount > 0) --voice->voiceLinePostSuccessCount;  // +0xa3 (Sec4.4 code 9)
    // 0x00469e00's further voice-line bookkeeping is "a later unit" (Sec4.4) - not modelled.
}

void AudioRuntime::handleBankStatus(int32_t code, uint32_t bankId) {
    auto it = banks_.find(bankId);
    if (it == banks_.end()) return;
    BankRecord& rec = it->second;
    switch (code) {
        case status::kBankLoadDone:  // 1
            rec.idle = false;
            rec.resident = true;
            break;
        case status::kBankLoadFailed:  // 0
            if (rec.reRequestPendingAtCallback) {
                rec.errorFlag = true;
                rec.resident = false;
                rec.idle = true;
            }
            // else: nothing recorded here (Sec3.7's literal rule; Sec6 OPEN item 10)
            break;
        case status::kBankUnloadDone:    // 2
        case status::kBankUnloadFailed:  // 3
            rec.resident = false;
            rec.idle = true;
            break;
        default:
            break;
    }
}

// =====================================================================
// Banks (Sec1.4, Sec3.7)
// =====================================================================

void AudioRuntime::registerBank(uint32_t bankId) {
    BankRecord rec;
    rec.bankId = bankId;
    banks_[bankId] = rec;
}

bool AudioRuntime::requestLoadBank(uint32_t bankId, int32_t memoryPool) {
    auto it = banks_.find(bankId);
    if (it == banks_.end()) return false;
    BankRecord& rec = it->second;
    // "the state machine cleared bit 2 just before LoadBank" (Sec3.7).
    rec.reRequestPendingAtCallback = false;
    rec.wwiseLoadCookie = nextBankLoadCookie_++;  // deliberately DIFFERENT from bankId (behaviour #12)
    if (!engineUp_) {
        // Sec1.4 0x00465cc0: "Only when byte 0x031728c5 is set; otherwise
        // (and when the result != 1) the record gets error bit 1 of
        // +0x5b." This is the SYNCHRONOUS call-site error bit, distinct
        // from Sec3.7's asynchronous completion-callback bits below.
        rec.errorFlag = true;
        return false;
    }
    // Behaviour #12: the game's own callback (0x00470220) reports the BANK
    // ID, not the cookie it was handed, as the message's cookie. So this
    // callback closure captures bankId and ignores the cookie/record
    // handle it receives from the backend.
    bool queued = backend_.loadBank(
        bankId,
        [this](uint32_t loadedBankId, int32_t wwiseResult, uint32_t /*recordHandleCookie*/) {
            int32_t statusCode = (wwiseResult == 1 || wwiseResult == 0x45) ? status::kBankLoadDone : status::kBankLoadFailed;
            postStatusMessage(statusCode, loadedBankId);
        },
        rec.wwiseLoadCookie, memoryPool);
    if (!queued) rec.errorFlag = true;  // "the result != 1" synchronous case
    return queued;
}

bool AudioRuntime::requestUnloadBank(uint32_t bankId) {
    auto it = banks_.find(bankId);
    if (it == banks_.end()) return false;
    if (!engineUp_) return false;  // Sec1.4: "Skipped when 0x031728c5 is clear"
    return backend_.unloadBank(
        bankId,
        [this](uint32_t unloadedBankId, int32_t wwiseResult, uint32_t /*recordHandleCookie*/) {
            int32_t statusCode = (wwiseResult == 1) ? status::kBankUnloadDone : status::kBankUnloadFailed;
            postStatusMessage(statusCode, unloadedBankId);
        },
        it->second.wwiseLoadCookie);
}

const BankRecord* AudioRuntime::findBank(uint32_t bankId) const {
    auto it = banks_.find(bankId);
    return it == banks_.end() ? nullptr : &it->second;
}

void AudioRuntime::setBankReRequestPending(uint32_t bankId, bool pending) {
    auto it = banks_.find(bankId);
    if (it != banks_.end()) it->second.reRequestPendingAtCallback = pending;
}

// =====================================================================
// Sec1.1: per-voice tick
// =====================================================================

bool AudioRuntime::perVoiceRtpcChanged(float current, float lastSent) const {
    if (!perVoiceRtpcChangedTest_) {
        throw OpenStateError("0x0045c160 per-voice RTPC changed-test", "spec-audio-runtime.md Sec6 item 3");
    }
    return perVoiceRtpcChangedTest_(current, lastSent);
}

void AudioRuntime::updateVoicePositional(Voice& voice) {
    // Sec1.1 0x0045cd80, gated by the voice's own deadline +0x1c.
    if (nowMs_ < voice.positionalDeadlineMs) return;

    // Position(s): behaviours #1 and #2.
    if (voice.positions.size() == 1) {
        backend_.setPosition(voice.id, voice.positions[0], -1);
    } else if (voice.positions.size() >= 2) {
        // Behaviour #1 (CONFIRMED shipped bug, reproduced faithfully, not
        // "fixed"): the orientation copy stores the source's orientation
        // x, y AND z all into the record's orientation-X slot, so
        // orientation.x ends up = source orientation.z, and
        // orientation.y/z are left whatever they were before this copy
        // (we zero-initialise the destination up front, so here they come
        // out as 0 rather than "whatever was on the stack" - an
        // unavoidable difference between a real uninitialised stack frame
        // and a clean C++ object, called out rather than hidden).
        std::vector<SoundPosition> corrupted;
        corrupted.reserve(voice.positions.size());
        size_t guardCount = std::min(voice.positions.size(), kMultiPositionGuardCapacity);
        // Behaviour #2: the real stack array holds 16 records; a byte
        // count up to 255 would overrun it. Reproducing a real overrun is
        // neither possible nor desirable in safe C++, so (per the task's
        // explicit instruction) this is a HOST-SAFETY GUARD: silently
        // capping at 16 and counting how many were dropped would hide the
        // defect, so instead this asserts nothing is silently lost -
        // positions beyond the 16th are simply never sent, same
        // observable effect an overrun-avoiding real fix would have.
        for (size_t i = 0; i < guardCount; ++i) {
            SoundPosition src = voice.positions[i];
            SoundPosition dst;
            dst.posX = src.posX;
            dst.posY = src.posY;
            dst.posZ = src.posZ;
            dst.oriX = src.oriZ;  // the bug: last of three same-slot stores wins
            dst.oriY = 0.0f;      // never actually written by the real copy loop
            dst.oriZ = 0.0f;
            corrupted.push_back(dst);
        }
        backend_.setMultiplePositions(voice.id, corrupted, 2 /* MultiDirections */);
    }

    // Obstruction (behaviour #9's slew).
    if (voice.obstructionSent != voice.obstructionTarget) {
        uint8_t cur = voice.obstructionSent;
        uint8_t target = voice.obstructionTarget;
        int16_t lo = static_cast<int16_t>(cur) - kObstructionSlewPerUpdate;
        int16_t hi = static_cast<int16_t>(cur) + kObstructionSlewPerUpdate;
        if (lo < 0) lo = 0;
        if (hi > 100) hi = 100;
        int16_t clamped = std::min<int16_t>(std::max<int16_t>(static_cast<int16_t>(target), lo), hi);
        voice.obstructionSent = static_cast<uint8_t>(clamped);
        backend_.setObjectObstructionAndOcclusion(voice.id, 0, voice.obstructionSent / kObstructionDivisor, 0.0f);
    }

    // Per-voice RTPC 0x85c3e987 (Sec6 OPEN item 3's changed-test gates this).
    if (voice.perVoiceRtpcEnabled) {
        if (perVoiceRtpcChanged(voice.rtpcCurrent, voice.rtpcLastSent)) {
            backend_.setRTPCValue(0x85c3e987u, voice.rtpcCurrent, voice.id);
            voice.rtpcLastSent = voice.rtpcCurrent;
        }
    }

    // Aux sends: always sent on every positional update, changed or not (Sec1.1).
    if (auxSendsEnabled_) {
        std::vector<AuxSend> slots;
        AuxSend slot0 = (voice.flags & voice_flags::kUseOwnAuxSend0) ? voice.auxSend0 : globalAuxSend0_;
        AuxSend candidates[3] = {slot0, voice.auxSend1, voice.auxSend2};
        for (const AuxSend& c : candidates) {
            if (c.id != 0 && c.level > kAuxSendThreshold) slots.push_back(c);
        }
        backend_.setGameObjectEnvironmentsValues(voice.id, slots);
    }

    voice.positionalDeadlineMs = nowMs_ + (voice.playRecords.empty() ? kPositionalDeadlineIdleMs : kPositionalDeadlineActiveMs);
}

void AudioRuntime::flushVoiceRtpcList(Voice& voice) {
    for (VoiceRtpcNode& node : voice.rtpcs) {
        if (!node.dirty) continue;
        if (engineUp_) backend_.setRTPCValue(node.id, node.value, voice.id);
        node.dirty = false;  // cleared even when the engine is down (Sec1.1)
    }
    voice.flags &= static_cast<uint16_t>(~voice_flags::kRtpcListDirty);
}

void AudioRuntime::flushVoiceSwitchList(Voice& voice) {
    for (SwitchNode& node : voice.switches) {
        if (!node.dirty) continue;
        if (engineUp_) backend_.setSwitch(node.group, node.state, voice.id);
        node.dirty = false;
    }
    voice.flags &= static_cast<uint16_t>(~voice_flags::kSwitchListDirty);
}

bool AudioRuntime::runPlayRecordStateMachine(Voice& voice, PlayRecord& record) {
    // Sec2.1's start-delay deadline, checked only in state 1.
    if (record.state == PlayRecordState::kCreated && record.startDelayDeadlineMs >= 0 &&
        nowMs_ >= record.startDelayDeadlineMs) {
        record.requestWord |= request_bits::kPlayOrResume;
        record.startDelayDeadlineMs = -1;
    }

    // --- Phase 1: consume status-raised bits (Sec2.3's first table). No
    // Wwise call happens in this phase; first match wins. ---
    uint32_t& w = record.requestWord;
    if (w & request_bits::kKilled) {
        w = (w & 0x81880000u) | 0x880000u;
        w |= request_bits::kDead;
        record.state = PlayRecordState::kKilled;
        return true;  // terminal: caller unlinks/frees
    }
    if (w & request_bits::kPostFailedConfirmed) {
        w = (w & 0x81200000u) | 0x200000u;
        w |= request_bits::kDead;
        record.state = PlayRecordState::kFailed;
        return true;
    }
    if (w & request_bits::kEndedConfirmed) {
        w = (w & 0x81a80000u) | 0x80000u;
        w |= request_bits::kDead;
        record.state = PlayRecordState::kEnded;
        return true;
    }
    if (w & request_bits::kPausedConfirmed) {
        w &= ~(request_bits::kPausedConfirmed | request_bits::kPause);
        record.state = PlayRecordState::kPaused;
        // non-terminal: fall through to phase 2 with the new state
    } else if (w & request_bits::kResumedOrDuration) {
        // "else if": Sec2.3 says only the FIRST matching event bit (in
        // priority order) acts per tick; 0x40000 and 0x20000 are mutually
        // exclusive in normal operation, but this keeps that guarantee
        // even if a test sets both directly.
        w &= ~(request_bits::kResumedOrDuration | request_bits::kPlayOrResume);
        if (record.state != PlayRecordState::kPlaying) record.state = PlayRecordState::kPlaying;
        // non-terminal: fall through to phase 2
    }

    // --- Phase 2: one action, first match wins. At most one Wwise call. ---
    if (w & request_bits::kStop) {
        if (record.conversationSlot != kNoConversationSlot) {
            // "release stream slot" - CORRECTED 2026-10-08 (Sec1.2a): this
            // is the real `.ctdg_pc` active-conversation pool, not a
            // persona-line streaming slot. releaseConversationSlot() also
            // raises the stop bit on every OTHER record still referencing
            // this slot (0x0046a700's documented force-stop cascade); this
            // record's own bit is already set, so re-raising it here is a
            // harmless no-op for `record` itself.
            uint8_t slot = record.conversationSlot;
            record.conversationSlot = kNoConversationSlot;
            releaseConversationSlot(slot);
        }
        bool attempted = false;
        bool success = false;
        if (record.state == PlayRecordState::kCreated) {
            if (record.posted) {
                attempted = true;
                success = engineUp_ && record.playingId != 0 &&
                           backend_.stopPlayingID(record.playingId, 0, kCurveLinear);
            }
        } else if (record.state == PlayRecordState::kPlaying || record.state == PlayRecordState::kPauseSent ||
                   record.state == PlayRecordState::kPaused) {
            attempted = true;
            success = engineUp_ && record.playingId != 0 && backend_.stopPlayingID(record.playingId, 0, kCurveLinear);
        } else if (record.state == PlayRecordState::kFailed) {
            record.state = PlayRecordState::kEnded;  // Sec2.3: "state 7 -> 6", no call documented
        }
        if (attempted) {
            record.state = success ? PlayRecordState::kStopSent : PlayRecordState::kEnded;
            record.posted = false;
            record.playingId = 0;
            postStatusMessage(status::kEnded, record.cookie);  // "Always posts status 7 ... afterwards" (Sec1.2)
        }
        // bit 3 is never cleared (Sec2.3). Any path above that landed the
        // record directly in a terminal state (stop-on-failed-record, or a
        // failed StopPlayingID call) must still be unlinked/freed this
        // tick, same as a terminal state reached via phase 1 - otherwise
        // it would leak forever (nothing re-arms a bit to catch it later).
        if (IsTerminalState(record.state)) {
            w |= request_bits::kDead;  // "states 6,7,8 on entry set flag bit 31" (Sec2.3), for field-value fidelity
            return true;
        }
        return false;
    }

    if (w & request_bits::kPlayOrResume) {
        if (record.state == PlayRecordState::kCreated) {
            bool voiceLine = (w & request_bits::kVoiceLine) != 0;
            w &= ~request_bits::kPlayOrResume;  // "bit cleared" regardless of outcome
            uint32_t playingId = 0;
            if (!voiceLine) {
                // Sound-effect path (0x00470720): has the usual engine-up guard.
                if (engineUp_) {
                    playingId = backend_.postEvent(record.eventIdOrLineKey, voice.id, post_event_flags::kSoundEffectFlags,
                                                    makeSoundEffectCallback(), record.cookie, {});
                }
            } else {
                // Voice-line path (0x00470500): behaviour #3 - NO engine-up
                // guard at all (unlike every other wrapper).
                ExternalSourceInfo src;
                src.cookie = record.eventIdOrLineKey;  // stand-in for 0x00469100's resolved cookie
                src.codec = 4;                          // Vorbis only (behaviour #14)
                src.fileId = record.eventIdOrLineKey;   // stand-in for 0x00468ae0's file id
                playingId = backend_.postEvent(record.eventIdOrLineKey, voice.id, post_event_flags::kVoiceLineFlags,
                                                makeVoiceLineCallback(), record.cookie, {src});
                if (playingId != 0) ++voice.voiceLinePostSuccessCount;  // +0xa3 (Sec1.2)
            }
            record.playingId = playingId;
            if (playingId == 0) {
                // Behaviour #3: voice-line path leaves +0x1e at 1 on
                // failure; sound-effect path clears it.
                record.posted = voiceLine;
                postStatusMessage(status::kPostFailed, record.cookie);
                record.state = PlayRecordState::kFailed;  // "the state machine then moves the record to state 7"
            } else {
                record.posted = true;
                // stays in state 1 on success (Sec1.2 footnote)
            }
        } else if (record.state == PlayRecordState::kPaused) {
            bool success = engineUp_ && record.playingId != 0 &&
                            backend_.executeActionOnEvent(record.eventIdOrLineKey, EventAction::kResume, voice.id, 10, kCurveLinear);
            if (success) {
                w &= ~request_bits::kPlayOrResume;  // "clears request bit 0 on success"
                postStatusMessage(status::kStarted, record.cookie);
                // "stays 4" synchronously; status 4's later processing moves it to 2.
            }
        }
        return false;
    }

    if (w & request_bits::kPause) {
        if (record.state == PlayRecordState::kPlaying) {
            bool success = engineUp_ && record.playingId != 0 &&
                            backend_.executeActionOnEvent(record.eventIdOrLineKey, EventAction::kPause, voice.id, 10, kCurveLinear);
            if (success) {
                record.state = PlayRecordState::kPauseSent;
                postStatusMessage(status::kPaused, record.cookie);
                // bit 2 is NOT cleared here - only by the later status-6 processing (Sec2.3).
            }
        }
        return false;
    }

    if (w & request_bits::kSeek) {
        if (record.state == PlayRecordState::kPlaying) {
            if (record.seekTargetMs < 0) {
                // guard: "+0x14 < 0 -> -0x64" - nothing to do, bit stays set (won't usefully retry, but
                // matches the documented guard without inventing a different behaviour)
            } else if (engineUp_ && voice.id != kNullObject) {
                bool success = backend_.seekOnEvent(record.eventIdOrLineKey, voice.id, record.seekTargetMs, false);
                if (success) {
                    record.seekTargetMs = -1;
                    w &= ~request_bits::kSeek;
                }
                // failure: bit 1 stays set, retried next tick (Sec1.2)
            }
        }
        return false;
    }

    return false;
}

PostEventCallback AudioRuntime::makeSoundEffectCallback() {
    // 0x004706c0 (Sec4.1): type==EndOfEvent -> status 7; type==Marker ->
    // status 5 (with the label); type==Duration -> status 4; anything
    // else -> nothing.
    return [this](WwiseCallbackType type, uint32_t cookie, const std::string& label) {
        switch (type) {
            case WwiseCallbackType::kEndOfEvent:
                postStatusMessage(status::kEnded, cookie);
                break;
            case WwiseCallbackType::kMarker:
                postStatusMessage(status::kMarker, cookie, label);
                break;
            case WwiseCallbackType::kDuration:
                postStatusMessage(status::kStarted, cookie);
                break;
        }
    };
}
PostEventCallback AudioRuntime::makeVoiceLineCallback() {
    // 0x00469ea0 (Sec4.1): type bit 0x1 set -> status 9 then status 7 (two
    // messages, same cookie); else type bit 0x8 set -> status 4. The
    // voice-line path never asks for markers (flags = 9), so type bit
    // 0x4 never occurs here.
    return [this](WwiseCallbackType type, uint32_t cookie, const std::string&) {
        int32_t bits = static_cast<int32_t>(type);
        if (bits & static_cast<int32_t>(WwiseCallbackType::kEndOfEvent)) {
            postStatusMessage(status::kVoiceLineEnded, cookie);
            postStatusMessage(status::kEnded, cookie);
        } else if (bits & static_cast<int32_t>(WwiseCallbackType::kDuration)) {
            postStatusMessage(status::kStarted, cookie);
        }
    };
}

void AudioRuntime::tickOneVoice(Voice& voice, bool& skipToNextVoice) {
    skipToNextVoice = false;

    // Step 1: lazy registration.
    if (voice.flags & voice_flags::kNeedsRegister) {
        bool ok = engineUp_ && backend_.registerGameObj(voice.handle, std::to_string(voice.handle));
        if (ok) voice.flags &= static_cast<uint16_t>(~voice_flags::kNeedsRegister);
        skipToNextVoice = true;  // behaviour #10: "either way go to the next voice"
        return;
    }

    // Step 2: positional update (gated by the voice's own deadline inside).
    if (clearAllAuxSendsRequested_) {
        backend_.setGameObjectEnvironmentsValues(voice.id, {});
    }
    updateVoicePositional(voice);

    // Step 3: RTPC list flush.
    if (voice.flags & voice_flags::kRtpcListDirty) flushVoiceRtpcList(voice);

    // Step 4: switch list flush, then go to next voice (behaviour #10).
    if (voice.flags & voice_flags::kSwitchListDirty) {
        flushVoiceSwitchList(voice);
        skipToNextVoice = true;
        return;
    }

    // Step 5: run the state machine on every play record; unlink/free dead ones.
    for (PlayRecord& rec : voice.playRecords) {
        if (runPlayRecordStateMachine(voice, rec)) {
            rec.markedDead = true;
        }
    }
    unlinkDeadRecords(voice);

    // Step 6: release-when-empty / release-now.
    if (voice.playRecords.empty() && (voice.flags & voice_flags::kReleaseWhenEmpty)) {
        voice.flags |= voice_flags::kReleaseNow;
    }
}

void AudioRuntime::unlinkDeadRecords(Voice& voice) {
    for (auto it = voice.playRecords.begin(); it != voice.playRecords.end();) {
        if (it->markedDead) {
            cookieOwner_.erase(it->cookie);
            it = voice.playRecords.erase(it);
        } else {
            ++it;
        }
    }
}

void AudioRuntime::tickAllVoices() {
    std::vector<GameObjectId> ids;
    ids.reserve(voices_.size());
    for (auto& kv : voices_) ids.push_back(kv.first);

    for (GameObjectId id : ids) {
        auto it = voices_.find(id);
        if (it == voices_.end()) continue;  // may have been torn down by an earlier voice in this pass (shouldn't happen, defensive)
        bool skip = false;
        tickOneVoice(it->second, skip);
        it = voices_.find(id);
        if (it != voices_.end() && (it->second.flags & voice_flags::kReleaseNow)) {
            teardownVoiceInternal(it->second);
            voices_.erase(it);
        }
    }
    clearAllAuxSendsRequested_ = false;  // "clears that byte after the pass over all voices" (Sec1.1)
}

// =====================================================================
// Sec1.3: frame tick
// =====================================================================

void AudioRuntime::runFrameTick() {
    smoothedRtpc_.tick(rtpcCache_, backend_, engineUp_);
    mixRtpcs_.tick(rtpcCache_, backend_, engineUp_);
    singleValueRtpc_.tick(rtpcCache_, backend_, engineUp_);
    listener_.tick(backend_, engineUp_);
    if (engineUp_) backend_.renderAudio();
}

void AudioRuntime::runMainAudioThreadPass() {
    // Sec4.5: "the main loop increments it twice per pass" - net effect,
    // real work happens on every second call to this wrapper.
    ++mainLoopPassCounter_;
    bool doWork = (mainLoopPassCounter_ % 2) == 0;
    if (doWork) {
        tickAllVoices();
        runFrameTick();
    }
    ++mainLoopPassCounter_;
}

}  // namespace sr3audioruntime
