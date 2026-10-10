#pragma once

// sr3audioruntime - the play record (spec-audio-runtime.md Sec2.1): one
// posted event on a voice, 32 bytes in the shipped game, modelled here as a
// plain struct keyed by the offsets the spec cites. The pool/free-list
// mechanics (Sec2.1's hash-by-cookie lookup table) are a storage detail of
// the real game's allocator and are not reproduced; AudioRuntime instead owns
// each voice's records directly (functionally equivalent: a freed record's
// late callback is dropped because AudioRuntime looks it up by (voice,
// cookie) and finds nothing, the same observable behaviour the spec
// describes for 0x004613a0's hash-table lookup).

#include <cstdint>

namespace sr3audioruntime {

// The `.ctdg_pc` active-conversation pool (spec-conversation-format.md
// Sec4-Sec6; spec-audio-runtime.md Sec1.2a, 2026-10-08). CORRECTED
// 2026-10-08: PlayRecord's +0x1c byte below was previously described as
// "the persona-line (`.lm_pc`) streaming slots" - that framing was wrong
// on BOTH halves. `.lm_pc` is not streamed audio at all (it is per-
// soundbank line-variant/weight metadata that picks WHICH take to play;
// the real audio lives in the persona's own voice soundbank media); and
// the three `0x58`-byte entries this field back-references are the
// `.ctdg_pc` active-conversation pool, exactly 3 slots (base
// `0x0322d828`, stride `0x58`, confirmed by five independent loop
// bounds), not a streaming mechanism at all. kNoConversationSlot is the
// sentinel for "none" and is also what
// AudioRuntime::acquireConversationSlot() returns when all
// kConversationPoolSize slots are busy.
constexpr uint8_t kNoConversationSlot = 0xff;
constexpr uint8_t kConversationPoolSize = 3;

// The request/flag word's bits (+0x18), Sec2.3. Names are HIGH, values
// CONFIRMED.
namespace request_bits {
constexpr uint32_t kPlayOrResume = 0x1;        // bit 0
constexpr uint32_t kSeek = 0x2;                // bit 1
constexpr uint32_t kPause = 0x4;                // bit 2
constexpr uint32_t kStop = 0x8;                 // bit 3
constexpr uint32_t kResumedOrDuration = 0x20000;  // status 4 raises this
constexpr uint32_t kPausedConfirmed = 0x40000;    // status 6 raises this
constexpr uint32_t kEndedConfirmed = 0x80000;     // status 7 raises this
constexpr uint32_t kPostFailedConfirmed = 0x200000;  // status 8 raises this
constexpr uint32_t kVoiceLine = 0x01000000;     // bit 24: selects the voice-line post path
constexpr uint32_t kKilled = 0x800000;          // raised by the voice API (Sec6 OPEN item 2)
constexpr uint32_t kDead = 0x80000000u;         // bit 31: set on entry to states 6/7/8
}  // namespace request_bits

// The separate state byte (Sec2.1's "state byte" array), values 1-8. Names
// are HIGH, from the 0x0045ce70/0x0045c9d0 transition tables.
enum class PlayRecordState : uint8_t {
    kCreated = 1,
    kPlaying = 2,
    kPauseSent = 3,
    kPaused = 4,
    kStopSent = 5,
    kEnded = 6,
    kFailed = 7,
    kKilled = 8,
};

inline bool IsTerminalState(PlayRecordState s) {
    return s == PlayRecordState::kEnded || s == PlayRecordState::kFailed || s == PlayRecordState::kKilled;
}

// One posted event on a voice (Sec2.1). Field comments give the real
// record's byte offset.
struct PlayRecord {
    uint32_t cookie = 0;             // +0x04: handle carried by every status message
    uint32_t eventIdOrLineKey = 0;   // +0x08: sound-effect event id, OR voice-line's line key
    uint32_t playingId = 0;          // +0x0c: 0 = not posted
    int32_t startDelayDeadlineMs = -1;  // +0x10: -1 = disarmed; fires -> raises kPlayOrResume (Sec2.1)
    int32_t seekTargetMs = -1;       // +0x14: < 0 = none
    uint32_t requestWord = 0;        // +0x18 (Sec2.3)
    // +0x1c: back-reference to which of (at most kConversationPoolSize)
    // concurrently-active scripted `.ctdg_pc` conversations posted this
    // record. kNoConversationSlot (0xff) = none. CORRECTED 2026-10-08
    // (spec-audio-runtime.md Sec1.2a) - NOT a "persona-line (.lm_pc)
    // streaming slot" as this field was previously described; see the
    // namespace-level comment above. Released through
    // AudioRuntime::releaseConversationSlot (0x0046a700 in the real game,
    // matched against entry `+0x50` of the pool's three `0x58`-byte
    // entries), which force-stops every play record still referencing
    // the slot before freeing it (0x0046a950).
    uint8_t conversationSlot = kNoConversationSlot;
    bool posted = false;             // +0x1e: set after PostEvent; left at 1 on voice-line post
                                      // failure (behaviour #3), cleared on sound-effect failure,
                                      // by the stop wrapper, and by status 7/8.
    bool hasStarted = false;         // +0x1f: set by status 4
    PlayRecordState state = PlayRecordState::kCreated;

    // Not part of the 32-byte record; AudioRuntime's own bookkeeping so a
    // dead record (bit 31) can be found and unlinked in the voice's own
    // tick step 5/6 (Sec2.4), as the real engine does with the per-state
    // lists.
    bool markedDead = false;
};

}  // namespace sr3audioruntime
