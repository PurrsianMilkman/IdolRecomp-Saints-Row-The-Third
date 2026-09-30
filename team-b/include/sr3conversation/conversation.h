// Reader for `.ctdg_pc` mission conversations (spec-conversation-format.md).
//
// This is the flattest format in the project: an 8-byte header, a fixed
// 32-byte source-name field, a bounded record count, and a packed array of
// 12-byte dialogue-turn records. There are NO offsets and NO pointer
// fixups anywhere in it (spec Sec3), which is unusual here - every other
// format this project reads has at least one offset field. Every byte of
// every shipped file is accounted for, and the whole structure is pinned
// by one exact-size identity:
//
//     44 + 12 * record_count == file size      (4,984/4,984 real files)
//
// parse() enforces that identity, so a successful parse IS that check
// re-run per file - the same "exact-size replay as structural proof"
// idiom used by sr3morph and sr3foliage.
//
// WHAT THIS READER DELIBERATELY DOES NOT DO:
//
//  * It does not resolve speaker ids to names. They are the rotate-6/XOR
//    engine hash (vpp::hashFilename - spec Sec6 confirms this
//    algebraically), but spec Sec6.1 records a FAILED naming attempt:
//    0 of 57 ids matched any of 3,917 curated candidates, with a control
//    also at 0. Recovering the names needs the validator's string table,
//    which the spec pass did not open. Surfaced raw; do not guess.
//  * It does not interpret the third per-turn field at record +0x08.
//    That the field is stored per turn is CONFIRMED; that it is a
//    millisecond timing offset is HYPOTHESIS only (spec Sec4), so it is
//    exposed by offset name with no interpretation applied.
//  * It does not implement the registry side of the load path. Note in
//    particular that the registry key is a table-driven CRC-32 over the
//    lowercased basename - NOT the rotate-6/XOR hash in vpp/hash.h. This
//    format uses BOTH hashes, for different purposes (spec Sec6); see
//    vpp/hash.h's own note. If a future pass needs the registry key, it
//    must implement CRC-32 separately rather than reaching for
//    vpp::hashFilename.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sr3conversation/errors.h"
#include "vpp/byte_view.h"

namespace sr3conversation {

using vpp::ByteView;

// "DCAV" in file order (spec Sec3). CONFIRMED - disassembly + 4,984/4,984.
constexpr uint32_t kMagic = 0x56414344u;
constexpr uint32_t kRequiredVersion = 1; // CONFIRMED - the consumer requires it

constexpr size_t kSourceNameOffset = 0x08;
constexpr size_t kSourceNameSize = 0x20;   // fixed-width, copied wholesale as 0x20 bytes
constexpr size_t kRecordCountOffset = 0x28;
constexpr size_t kHeaderSize = 0x2C;       // 44 - records start here
constexpr size_t kRecordSize = 12;

// The consumer rejects a count of 0 and anything above this before it
// touches a single record (spec Sec5). CONFIRMED - disassembly.
constexpr uint32_t kMaxRecordCount = 30;

// One 12-byte dialogue turn (spec Sec4). Records are consumed strictly in
// file order, one runtime node per record, so RECORD ORDER IS CONVERSATION
// ORDER (CONFIRMED - disassembly).
struct DialogueTurn {
    // Hashed participant name. The consumer gathers the distinct values of
    // this field into a set and validates each ONCE per conversation; a
    // failure aborts playback. Only 57 distinct values exist across all
    // 19,440 shipped records. The hash is the rotate-6/XOR one, but the
    // pre-image names are OPEN - see the header comment.
    uint32_t speakerId = 0;

    // Resolved together with the speaker as the pair (speaker, line) -
    // which is what makes this a per-speaker line reference rather than a
    // global one (CONFIRMED - disassembly). What it resolves against (an
    // audio bank? a subtitle table?) is OPEN (spec Sec9 item 2).
    uint32_t lineId = 0;

    // CONFIRMED that this is stored per turn; its MEANING is HYPOTHESIS
    // only (spec Sec4: zero in 19,109 of 19,440 records, the rest signed
    // multiples of 100 spanning roughly -500...+600, which fits a
    // millisecond timing offset). Named by offset and surfaced raw rather
    // than as "timingMs", per this project's confidence discipline.
    uint32_t field_0x08_raw = 0;

    // Convenience reinterpretation of field_0x08_raw as a signed 32-bit
    // value. This is a pure bit-level reading, NOT a semantic claim: the
    // observed non-zero values only look like small signed multiples of
    // 100 under this reading, which is what motivates the millisecond
    // hypothesis. Deciding what it MEANS is still the caller's problem.
    int32_t field_0x08_asInt32() const;
};

class Conversation {
public:
    // Parses a whole .ctdg_pc. Throws FormatError if the magic or version
    // is wrong, if the record count is 0 or above the loader's hard limit
    // of 30, or if `content` is not exactly `44 + 12 * count` bytes - all
    // four are rules the real consumer enforces (spec Sec3/Sec5), the last
    // one holding across 4,984/4,984 shipped files.
    static Conversation parse(ByteView content);

    uint32_t version() const { return version_; }

    // The authoring-source filename (`<name>.ctd`), from the fixed 32-byte
    // field at +0x08. TREAT THIS AS A LABEL, NOT A KEY: it is truncated at
    // 31 characters in 1,187 shipped files (spec Sec1), so distinct
    // conversations can and do share a source name. The `.ctd` authoring
    // extension is the same authoring-vs-shipped split seen at
    // `.fmeshx` -> `.cfmesh_pc` and `.animx` -> `.anim_pc`.
    const std::string& sourceName() const { return sourceName_; }

    // True if the source name shows the truncation signature: a length of
    // 31 (the engine clips an over-long authoring name to 31 characters
    // and still writes the terminator) or 32 (no terminator at all, which
    // does not occur in shipped data). Exposed so a caller can tell a
    // complete name from a clipped one rather than silently trusting
    // sourceName().
    //
    // NECESSARILY IMPRECISE, and deliberately so: a name that genuinely is
    // exactly 31 characters is byte-for-byte indistinguishable from one
    // clipped to 31, because the clip discards the evidence. This reports
    // "at the clip limit", which is the strongest claim the bytes support
    // - not "definitely truncated". Spec Sec1 counts 1,187 such files and
    // concludes the field is a label, not a reliable key; treat a true
    // return as "do not use sourceName() as an identifier here".
    bool sourceNameTruncated() const { return sourceNameTruncated_; }

    const std::vector<DialogueTurn>& turns() const { return turns_; }

    // The distinct speaker ids in first-appearance order. This mirrors what
    // the real consumer builds before playback (a set it validates once per
    // conversation, capacity 30 - spec Sec4), so it is a genuine structural
    // feature of the format rather than a convenience this reader invented.
    // Shipped files hold 1-4 distinct speakers; 4,149 of 4,984 are
    // two-handers.
    std::vector<uint32_t> distinctSpeakerIds() const;

private:
    uint32_t version_ = 0;
    std::string sourceName_;
    bool sourceNameTruncated_ = false;
    std::vector<DialogueTurn> turns_;
};

} // namespace sr3conversation
