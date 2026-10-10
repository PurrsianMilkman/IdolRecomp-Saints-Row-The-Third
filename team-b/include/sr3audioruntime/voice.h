#pragma once

// sr3audioruntime - the voice (spec-audio-runtime.md Sec2.2): the Wwise game
// object, stride 0xa8 in the shipped game. Modelled as a plain struct; the
// pool/free-list mechanics are a storage detail (see play_record.h's header
// comment for the same point) and are not reproduced.
//
// Sec6 OPEN item 1 says the voice API (who sets positions, obstruction
// target, aux sends, the per-voice RTPC value/enable, persona keys, and the
// marker callback) is a later, out-of-scope unit; this library only reads
// those fields once set. The setters below exist so a harness/test can play
// that later unit's role; they do not claim the writer's identity or timing.

#include <cstdint>
#include <string>
#include <vector>

#include "sr3audioruntime/play_record.h"
#include "sr3audioruntime/types.h"

namespace sr3audioruntime {

// Voice flags word (+0x98), Sec2.3. Names are HIGH, values CONFIRMED.
namespace voice_flags {
constexpr uint16_t kUseOwnAuxSend0 = 0x0001;
constexpr uint16_t kRtpcListDirty = 0x0010;
constexpr uint16_t kSwitchListDirty = 0x0020;
constexpr uint16_t kNeedsRegister = 0x0040;
constexpr uint16_t kReleaseWhenEmpty = 0x4000;
constexpr uint16_t kReleaseNow = 0x8000;
}  // namespace voice_flags

// One chained switch-list node (+0x78.. / Sec2.2).
struct SwitchNode {
    uint32_t group = 0;
    uint32_t state = 0;
    bool dirty = false;
};

// One chained RTPC-list node (+0x88.. / Sec2.2).
struct VoiceRtpcNode {
    uint32_t id = 0;
    float value = 0.0f;
    bool dirty = false;
};

// Sec2.2's marker-callback pair, +0x04/+0x08, gated by +0x9e bit 0 (Sec4.4
// code 5).
using MarkerCallback = std::function<void(uint32_t playingId, const std::string& label)>;

struct Voice {
    GameObjectId id = kNullObject;    // assigned by AudioRuntime::createVoice; stands in for the
                                       // voice's own address (Sec2.2 header note).
    uint64_t handle = 0;              // +0x00: nonzero = live. Registration name is this printed
                                       // with "%u" (Sec1.1 0x00470610).
    std::string debugName;            // not part of the record; for test/log readability only.

    float rtpcCurrent = 0.0f;         // +0x0c: per-voice RTPC 0x85c3e987, current value
    float rtpcLastSent = 0.0f;        // +0x10: last value actually sent

    uint32_t primaryPersonaKey = 0;   // +0x14
    uint32_t fallbackPersonaKey = 0;  // +0x18

    // +0x1c: positional-update deadline, a millisecond timer; negative =
    // disarmed. AudioRuntime uses its own simulated millisecond clock
    // (advanceTimeMs) rather than the real wrapping 0x01320d94 counter.
    // Starts at 0 ("already due"), so a freshly created voice's first tick
    // performs its positional update immediately, same as every other
    // freshly-zeroed piece of state in this struct (flags, request words):
    // the spec does not say who arms this before the first tick, so "due
    // now" rather than "disarmed forever" is this project's structural
    // default, not a claimed engine value.
    int64_t positionalDeadlineMs = 0;

    // +0x24 (list head) / +0x28.. / +0x44.. (chained nodes): the full
    // ordered set of emitter positions. size()==0: nothing sent; size()==1:
    // SetPosition; size()>=2: SetMultiplePositions (+0x9f is positions.size()
    // before any guard is applied - see behaviour #2 in audio_runtime.h).
    std::vector<SoundPosition> positions;

    std::vector<PlayRecord> playRecords;  // +0x24 list / +0xa0 count == playRecords.size()

    AuxSend auxSend0;  // +0x60/+0x64
    AuxSend auxSend1;  // +0x68/+0x6c
    AuxSend auxSend2;  // +0x70/+0x74

    std::vector<SwitchNode> switches;    // +0x78..
    std::vector<VoiceRtpcNode> rtpcs;    // +0x88..

    uint16_t flags = 0;  // +0x98

    uint8_t obstructionSent = 0;    // +0xa1 (0-100)
    uint8_t obstructionTarget = 0;  // +0xa2 (0-100)
    uint8_t voiceLinePostSuccessCount = 0;  // +0xa3

    bool perVoiceRtpcEnabled = false;  // +0xa6

    bool markerCallbackEnabled = false;  // +0x9e bit 0
    MarkerCallback markerCallback;        // +0x04/+0x08 pair
};

}  // namespace sr3audioruntime
