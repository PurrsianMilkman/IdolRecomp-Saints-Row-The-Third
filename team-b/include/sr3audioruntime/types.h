#pragma once

// sr3audioruntime - shared value types for the audio runtime contract
// (spec-audio-runtime.md, "Audio Runtime: the Wwise Adapter Contract").
//
// This library is the GAME SIDE of the Wwise adapter boundary: it implements
// the 25 calling-function crossings of Sec1, the two record layouts of Sec2,
// the init/shutdown order of Sec3, and the callback/feedback path of Sec4,
// against an abstract WwiseBackend (wwise_backend.h) - the contract a real
// Wwise adapter (or, here, a null/logging adapter) must satisfy. It is NOT a
// Wwise SDK integration, does not decode or play audio, and does not open any
// Wwise-internal or bank-HIRC material (spec Sec0, Sec7 clean-room check).

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sr3audioruntime {

// The Wwise "game object" id. In the shipped game this is the voice's own
// address (spec-audio-runtime.md Sec2.2: "The game object id handed to Wwise
// is the voice's address"); here it is an opaque handle assigned by
// AudioRuntime::createVoice(). 0 = no object / null voice (Sec1.1 guards:
// "voice null -> -0x64"). -1 = the global (non-object) target used by every
// global SetRTPCValue crossing (Sec1.3: "object = EAX, or -1 when EAX = 0").
using GameObjectId = int64_t;
constexpr GameObjectId kNullObject = 0;
constexpr GameObjectId kGlobalObject = -1;

// Negative game-side error codes, named exactly as spec-audio-runtime.md Sec1
// states them ("Reading conventions": "Every game-side error code is
// negative"). Only the codes actually cited by a Sec1/Sec1.2/Sec1.3 guard row
// are given names; nothing here is invented.
namespace errc {
constexpr int32_t kEngineNotUp = -0x12e;          // byte 0x031728c6 clear (Sec1 "Reading conventions")
constexpr int32_t kMissingVoice = -0x64;          // "missing voice" (Sec1 conventions; also Sec1.2 SeekOnEvent +0x14<0 case)
constexpr int32_t kMissingRecordOrEvent = -0x67;  // "missing play record or event" (Sec1 conventions)
constexpr int32_t kVoiceHandleZero = -0x65;       // "voice handle 0" (Sec1 conventions)
constexpr int32_t kGenericWwiseFailure = -0x63;   // generic failure of the Wwise call (Sec1 conventions)
constexpr int32_t kRegisterGameObjFailed = -0x132;  // 0x00470610: Wwise result != 1 (Sec1.1)
constexpr int32_t kSetRTPCValueFailed = -0x140;     // 0x00470960 / 0x0045f2d0: Wwise result != 1 (Sec1.1/Sec1.3)
constexpr int32_t kPostFailedZeroPlayingId = -0x137;  // 0x00470720/0x00470500: zero playing id (Sec1.2)
constexpr int32_t kVoiceLineNoFileId = -0x17;        // 0x00470500: both file ids 0 (Sec1.2)
constexpr int32_t kSeekFailed = -0x13c;              // 0x00470790: Wwise result != 1 (Sec1.2)
constexpr int32_t kVoiceApiGuardOff = -1;            // 0x0045f2d0/0x00462970: voice-API byte clear (Sec1.3)
constexpr int32_t kOk = 0;
}  // namespace errc

// Wwise `AkSoundPosition` order (Sec2.2 +0x28..+0x3c): position x,y,z then
// orientation x,y,z.
struct SoundPosition {
    float posX = 0, posY = 0, posZ = 0;
    float oriX = 0, oriY = 0, oriZ = 0;
};

// Public `AkListenerPosition` order (Sec1.3 0x0045f690 row, HIGH): front,
// top, position.
struct ListenerPosition {
    float frontX = 0, frontY = 0, frontZ = 0;
    float topX = 0, topY = 0, topZ = 0;
    float posX = 0, posY = 0, posZ = 0;
};

// One `SetGameObjectEnvironmentsValues` slot: {environment id, float level}
// (Sec1.1 row 0x004709b0).
struct AuxSend {
    uint32_t id = 0;
    float level = 0.0f;
};

// One `PostEvent` external-source array entry (Sec1.2 row 0x00470500): codec
// is always 4 (Vorbis, behaviour #14); file name/in-memory fields are never
// used by the voice-line path (null/0), so only the file id is modelled.
struct ExternalSourceInfo {
    uint32_t cookie = 0;
    uint32_t codec = 4;  // AK codec id 4 = Vorbis (Sec1.2, Sec3.2 RegisterCodec, behaviour #14)
    uint32_t fileId = 0;
};

// `PostEvent` flag bits used by the two posting paths (Sec1.2).
namespace post_event_flags {
constexpr uint32_t kEndOfEvent = 0x1;
constexpr uint32_t kMarker = 0x4;
constexpr uint32_t kDuration = 0x8;
constexpr uint32_t kEnableGetSourcePlayPosition = 0x10000;
constexpr uint32_t kSoundEffectFlags = kEndOfEvent | kMarker | kDuration | kEnableGetSourcePlayPosition;  // 0x1000d
constexpr uint32_t kVoiceLineFlags = kEndOfEvent | kDuration;  // 9
}  // namespace post_event_flags

// `ExecuteActionOnEvent` action ids (Sec1.2: 1 = Pause, 2 = Resume).
enum class EventAction : int32_t { kPause = 1, kResume = 2 };

// Every curve argument in Sec1.2 is the literal value 4 (Linear, HYPOTHESIS
// name for the CONFIRMED value).
constexpr int32_t kCurveLinear = 4;

// Wwise callback "type" values tested by the two PostEvent callbacks
// (Sec4.1). Public `AkCallbackType` names, HYPOTHESIS; values CONFIRMED.
enum class WwiseCallbackType : int32_t {
    kEndOfEvent = 1,
    kMarker = 4,
    kDuration = 8,
};

// The status codes carried through the message queue (Sec4.1/Sec4.2/Sec4.4).
// 0-3 are bank completions (Sec3.7), 4-9 are play-record codes (Sec4.1/4.4),
// 10 is the I/O hook's read-completion (Sec3.6).
namespace status {
constexpr int32_t kBankLoadFailed = 0;
constexpr int32_t kBankLoadDone = 1;
constexpr int32_t kBankUnloadDone = 2;
constexpr int32_t kBankUnloadFailed = 3;
constexpr int32_t kStarted = 4;    // "audible / resumed" (Duration or successful Resume)
constexpr int32_t kMarker = 5;
constexpr int32_t kPaused = 6;
constexpr int32_t kEnded = 7;      // ended or stopped
constexpr int32_t kPostFailed = 8;
constexpr int32_t kVoiceLineEnded = 9;
constexpr int32_t kIoCompletion = 10;
}  // namespace status

// A PostEvent callback, as the two concrete callback bodies 0x004706c0
// (sound effect) and 0x00469ea0 (voice line) are shaped by Sec4.1: invoked
// with (callback type, cookie, marker label-or-empty). The WwiseBackend
// contract stores this per playing id and a harness/test fires it to
// simulate "whichever Wwise thread delivers callbacks" (Sec4.2, out of
// scope in itself).
using PostEventCallback = std::function<void(WwiseCallbackType type, uint32_t cookie, const std::string& markerLabel)>;

// A bank-load/unload completion callback (Sec3.7): (bank id, Wwise result,
// cookie). Per behaviour #12 the game's own callback bodies use the bank id,
// not the cookie, as the message's cookie field - that remapping happens in
// AudioRuntime, not here.
using BankCallback = std::function<void(uint32_t bankId, int32_t wwiseResult, uint32_t cookie)>;

}  // namespace sr3audioruntime
