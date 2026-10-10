#pragma once

// sr3audioruntime - Vorbis-source wrapper glue (spec-audio-runtime.md
// Sec11, unit U-W4a: CAkSrcBankVorbis / CAkSrcFileVorbis; Sec11.9, unit
// U-W4b: the real decode-trigger slot, added in a later spec sync -
// commit 6637644, "audio-runtime 11.9: Wwise Vorbis decode trigger (vtable
// slot 10) + decode-loop worker"). Extends behaviour #14's existing "the
// only codec registered is 4/Vorbis... voice lines streamed by file id"
// stand-in (types.h's ExternalSourceInfo::codec, audio_runtime.cpp's
// src.codec = 4) with the real CONFIRMED container/source-split/decode-
// trigger structure Sec11 found underneath that stand-in.
//
// Scope, matching both units' own boundaries exactly:
//   - the RIFF/WAVE container Wwise wraps its Vorbis payload in (Sec11.7.1,
//     CONFIRMED by direct byte read of the "RIFF"/"WAVE"/"fmt "/"cue "/
//     "data"/"LIST" FourCC immediates in the shared helper 0x00f8fcb0);
//   - the CONFIRMED Bank-vs-File split in how each class gets its
//     COMPRESSED bytes (Sec11.7.2): Bank from a resident pointer+length
//     pair, File through a per-instance stream-descriptor object with a
//     growable compressed-data lookahead buffer, refilled on demand;
//   - File's dedicated seek slot vs Bank's lack of one (Sec11.8, HIGH:
//     "flush lookahead, reissue a read at the new offset" for File; a
//     cheap absorbed cursor recompute for Bank);
//   - Sec11.9's headline: vtable SLOT 10 on both classes is the real
//     GetBuffer-equivalent decode trigger, not just lookahead management
//     (Sec11.9.1.1's "part one" IS lookahead/output-buffer allocation,
//     count<<0xb = count*2048 bytes; Sec11.9.1.2's "part two" - previously
//     unread - calls a shared decode worker (0x00fa49e0, Sec11.9.2) that
//     writes decoded PCM into that buffer, fills a caller-supplied output
//     struct (buffer ptr/+0x0, capacity/+0x4, a fixed format word/+0xc,
//     decoded sample count/+0xe, status/+0x340 - the +0xe..+0x340 gap is
//     itself still OPEN, Sec11.9.1.2's own closing note), and advances the
//     per-instance sample-accurate playback position.
//
// Explicitly NOT modelled (document-and-defer, never invented - per
// Sec11.9.5's own "what's deliberately NOT claimed" and the coordinator's
// standing instruction on this unit): the real Vorbis-BITSTREAM-specific
// internals (codebook/IMDCT/windowing) `0x00fa49e0`'s own closure reaches
// (`0x00fa4950`/`0x00fa4810`/`0x00fa59a0`, Sec11.9.5 - "real, substantial,
// floating-point-bearing code... left as leads for a dedicated decode-
// core-internals pass, not claimed as understood here"). VorbisDecodeLoopWorker
// below is ONLY the seam a future pass plugs that piece into - no concrete
// Vorbis bitstream math lives HERE (it now lives in the real, vendored
// libvorbis, third_party/libvorbis - see wwise_vorbis_setup.h/
// real_vorbis_decode.h for the audio-bring-up task that wires the two
// together).
//
// UPDATE (audio bring-up task, 2026-10-08): the Wwise-SPECIFIC framing layer
// this comment used to say was "still Team A's to spec" HAS now landed,
// spec-audio-runtime.md Sec11.11 (unit U-W4c) - packet framing (a bare
// 16-bit little-endian length prefix, no Ogg pages, Sec11.11.1), the
// proprietary 42-byte "vorb" RIFF chunk (block-size exponents + a
// resource-identity hash + a sample-count fallback, Sec11.11.2), and the
// stripped/rebuilt setup-packet mechanism (Sec11.11.3). See
// wwise_vorbis_setup.h for that CONFIRMED layer, kept in a separate file
// (not added to VorbisContainerChunks below) because it is new,
// additively-sourced spec content this file's own original author never
// saw - touching this struct itself risked disturbing already-reviewed
// code for no functional reason. The exact bit-level relationship between
// Wwise's stripped setup packet and a standard Vorbis setup header's own
// bitstream content remains the one genuine OPEN item Sec11.11.3 itself
// names ("the actual Vorbis-spec-shaped bitstream parse... is out of scope
// per the task's own boundary") - real_vorbis_decode.h tests one concrete,
// well-motivated hypothesis about it empirically, against real libvorbis,
// and reports the real result rather than asserting an answer.
//
// Also not modelled here:
//   - the exact byte-level ring-buffer growth algorithm of the real
//     compressed-data lookahead fill (0x00fa3e90's own first half) - only
//     the documented BEHAVIOUR ("grow then pull more compressed bytes") is
//     modelled, via a plain resize-then-pull, not the original's specific
//     copy pattern;
//   - the exact arithmetic inside the real seek work (0x00fa36b0,
//     including the "+0x34 correction term added to the caller's output"
//     detail, Sec11.8) - not traced in the spec, so not invented here.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "sr3luahost/open_state.h"

namespace sr3audioruntime {

// Result of walking a RIFF/WAVE container for the sub-chunks Sec11.7.1
// CONFIRMED the real helper (0x00f8fcb0) recognises: "fmt ", "cue ",
// "data", "LIST". Standard RIFF chunk framing (FourCC + little-endian u32
// size, word-aligned) is public file-format knowledge, not derived from any
// leaked source; only the SET of recognised tags and the "reject anything
// that isn't RIFF/WAVE" behaviour are the binary's own CONFIRMED facts.
struct VorbisContainerChunks {
    bool valid = false;
    // Sec11.7.1: "early-exit... returning result 7" for anything that
    // doesn't start with a valid RIFF/WAVE header.
    int32_t rejectCode = 0;

    bool hasFmt = false;
    size_t fmtOffset = 0, fmtSize = 0;
    bool hasCue = false;
    size_t cueOffset = 0, cueSize = 0;
    bool hasData = false;
    size_t dataOffset = 0, dataSize = 0;
    bool hasList = false;
    size_t listOffset = 0, listSize = 0;
};

// Sec11.7.1: walks a "RIFF"/"WAVE" container, locating "fmt "/"cue "/
// "data"/"LIST" sub-chunks; rejects (rejectCode = 7) anything that doesn't
// start with a valid RIFF/WAVE header. Any chunk tag not in that set is
// skipped over by its own declared size (ordinary RIFF traversal), matching
// Sec11.7.1's own framing that the helper "recognises" exactly those four
// alongside the mandatory RIFF/WAVE pair.
inline VorbisContainerChunks parseRiffWaveContainer(const uint8_t* data, size_t size) {
    VorbisContainerChunks out;
    if (data == nullptr || size < 12) {
        out.rejectCode = 7;
        return out;
    }
    auto tag = [&](size_t off) { return (data[off]) | (data[off + 1] << 8) | (data[off + 2] << 16) | (data[off + 3] << 24); };
    auto u32 = [&](size_t off) -> uint32_t {
        return static_cast<uint32_t>(data[off]) | (static_cast<uint32_t>(data[off + 1]) << 8) |
               (static_cast<uint32_t>(data[off + 2]) << 16) | (static_cast<uint32_t>(data[off + 3]) << 24);
    };
    constexpr int32_t kRIFF = 0x46464952;  // "RIFF"
    constexpr int32_t kWAVE = 0x45564157;  // "WAVE"
    constexpr int32_t kFMT_ = 0x20746d66;  // "fmt "
    constexpr int32_t kCUE_ = 0x20657563;  // "cue "
    constexpr int32_t kDATA = 0x61746164;  // "data"
    constexpr int32_t kLIST = 0x5453494c;  // "LIST"

    if (tag(0) != kRIFF || tag(8) != kWAVE) {
        out.rejectCode = 7;
        return out;
    }

    size_t offset = 12;
    while (offset + 8 <= size) {
        int32_t chunkTag = tag(offset);
        uint32_t chunkSize = u32(offset + 4);
        size_t chunkDataOffset = offset + 8;
        if (chunkDataOffset > size) break;  // truncated chunk header
        size_t available = size - chunkDataOffset;
        size_t clampedSize = std::min<size_t>(chunkSize, available);

        if (chunkTag == kFMT_) {
            out.hasFmt = true;
            out.fmtOffset = chunkDataOffset;
            out.fmtSize = clampedSize;
        } else if (chunkTag == kCUE_) {
            out.hasCue = true;
            out.cueOffset = chunkDataOffset;
            out.cueSize = clampedSize;
        } else if (chunkTag == kDATA) {
            out.hasData = true;
            out.dataOffset = chunkDataOffset;
            out.dataSize = clampedSize;
        } else if (chunkTag == kLIST) {
            out.hasList = true;
            out.listOffset = chunkDataOffset;
            out.listSize = clampedSize;
        }

        size_t advance = chunkSize + (chunkSize & 1u);  // RIFF word-alignment padding
        if (chunkDataOffset + advance <= offset) break;  // guard against a zero/overflowing advance
        offset = chunkDataOffset + advance;
    }

    out.valid = true;
    return out;
}

// Sec11.9.1.2's CONFIRMED caller-supplied output-struct fields (the
// "GetBuffer"-shaped slot 10 writes these before returning). The large gap
// between +0xe and +0x340 is itself flagged OPEN by Sec11.9 ("either the
// real struct is simply this large... or +0x340 belongs to a different,
// adjacent object" - not resolved) - not modelled here beyond the fields
// Sec11.9.1.2 shows being written.
struct VorbisDecodeOutput {
    uint8_t* bufferPointer = nullptr;  // output struct +0x0, CONFIRMED
    uint32_t bufferCapacity = 0;       // output struct +0x4, CONFIRMED
    // CONFIRMED literal for Bank (0x400); File's slot 10 is "same shape,
    // parameterised for File's own field offsets" per Sec11.9.1.2 but the
    // literal itself was not independently re-shown for File - used here
    // as the HIGH-confidence shared default, not an invented value.
    uint16_t formatWord = 0;
    uint16_t decodedSampleCount = 0;  // output struct +0xe, CONFIRMED
    uint32_t statusCode = 0;          // output struct +0x340, CONFIRMED to exist
};

// Sec11.9.1.2: Bank's own CONFIRMED literal for the output struct's +0xc
// "fixed format/status word".
constexpr uint16_t kVorbisDecodeFormatWordBank = 0x400;

// Sec11.9.2's own CONFIRMED status vocabulary for the shared decode loop.
namespace vorbis_decode_status {
constexpr uint32_t kNoMoreData = 2;    // "je <end: status=2, 'no more data'>"
constexpr uint32_t kFinalize = 0x2b;   // "je <end: finalize>"
}  // namespace vorbis_decode_status

// Sec11.9.2: the shared decode-loop worker (0x00fa49e0) both classes' slot
// 10 call into (CONFIRMED exhaustive - exactly 3 static call sites, all
// inside slot 10's own two per-class bodies, Sec11.9.2's own byte-pattern
// scan). This is ONLY the seam: the real Vorbis-bitstream internals
// (codebook/IMDCT/windowing, reached through 0x00fa4950/0x00fa4810/
// 0x00fa59a0, Sec11.9.5) and the Wwise-specific framing on top of them
// (packet framing without Ogg pages, rebuilt setup headers, codebook
// selection) are explicitly NOT implemented here - that framing is still
// Team A's to spec (coordinator note, 2026-10-07), and the bitstream math
// itself is planned to come from real libvorbis (BSD-licensed, not a
// clean-room concern) once that framing lands. A future pass implements
// this interface; nothing here invents what it does.
class VorbisDecodeLoopWorker {
public:
    virtual ~VorbisDecodeLoopWorker() = default;

    // Sec11.9.2's own CONFIRMED loop contract (cdecl, 4 args: decode-state
    // pointer, format word, requested sample count, output buffer) -
    // `decodeState` is the source instance's own private 184-byte region
    // (Sec11.3.2), opaque to this interface. Fills at most
    // `outputBufferCapacity` bytes of `outputBuffer`, sets
    // `decodedSampleCountOut` to the samples actually produced (each
    // sample advances the output pointer by exactly 2 bytes - the 16-bit
    // PCM tell, Sec11.9.2), and returns a status: `vorbis_decode_status::
    // kNoMoreData`, `::kFinalize`, or any other value meaning "produced
    // `decodedSampleCountOut` samples, call again for more."
    virtual uint32_t decode(void* decodeState, uint16_t formatWord, uint32_t requestedSamples, uint8_t* outputBuffer,
                             uint32_t outputBufferCapacity, uint16_t& decodedSampleCountOut) = 0;
};

[[noreturn]] inline void throwVorbisDecodeConfigOpen(const char* what) {
    throw sr3luahost::OpenStateError(
        what, "spec-audio-runtime.md Sec11.9 (unit U-W4b): the decode trigger (vtable slot 10) and its "
              "shared decode-loop worker (0x00fa49e0) are CONFIRMED to exist, but the real Vorbis-bitstream "
              "internals and the Wwise-specific packet-framing/codebook-selection layer on top are not "
              "implemented in this clean-room library (Team A's packet-framing/codebook-selection spec is "
              "still open as of this writing; real decode math is planned to come from libvorbis once it "
              "lands) - never invented here");
}

// Sec11.9.1's CONFIRMED shared two-part shape ("part one": allocate the
// output PCM buffer once, count<<0xb = count*2048 bytes, Sec11.9.1.1;
// "part two": call the shared decode worker and fill the caller's output
// struct, Sec11.9.1.2), factored out so both VorbisBankSource and
// VorbisFileSource reuse it rather than duplicating the trigger logic -
// each class still owns its own COMPRESSED-data acquisition (resident vs.
// streamed, Sec11.7.2), which is genuinely per-class and stays on the
// source classes below.
class VorbisDecodeTrigger {
public:
    // Models `[this+0x40]`, the per-instance output-buffer size-in-2048-
    // byte-blocks field Sec11.9.1.1 confirms exists and drives the
    // allocation (`count << 0xb`) - no spec text gives it a default value,
    // so it stays OPEN until a caller sets one (never invented).
    void setOutputBlockCount(uint32_t blocks) { blockCount_ = blocks; }
    void setDecodeLoopWorker(std::shared_ptr<VorbisDecodeLoopWorker> worker) { worker_ = std::move(worker); }
    void setFormatWord(uint16_t formatWord) { formatWord_ = formatWord; }

    // Sec11.9.1.1 part one + Sec11.9.1.2 part two, in order: allocate the
    // output buffer once (reused on later calls, matching "if the buffer
    // pointer is already non-null, skip straight to part two"), then call
    // the decode worker and fill the output struct, advancing the
    // sample-accurate playback position by the decoded count (Sec11.9.1.2:
    // "add [esi+0xdc], eax").
    VorbisDecodeOutput decodeNextBlock(void* decodeState, uint32_t requestedSamples) {
        if (!blockCount_.has_value()) throwVorbisDecodeConfigOpen("CAkSrcVorbis output-block-count ([this+0x40])");
        if (!worker_) throwVorbisDecodeConfigOpen("CAkSrcVorbis decode-loop worker (0x00fa49e0)");

        const uint32_t capacity = *blockCount_ << 0xb;  // Sec11.9.1.1: count * 2048, CONFIRMED formula
        if (outputBuffer_.size() != capacity) outputBuffer_.resize(capacity);

        VorbisDecodeOutput out;
        out.bufferPointer = outputBuffer_.data();
        out.bufferCapacity = capacity;
        out.formatWord = formatWord_;
        out.statusCode = worker_->decode(decodeState, formatWord_, requestedSamples, outputBuffer_.data(), capacity,
                                          out.decodedSampleCount);
        playbackSamplePosition_ += out.decodedSampleCount;
        return out;
    }

    uint64_t playbackSamplePosition() const { return playbackSamplePosition_; }
    void resetPlaybackSamplePosition() { playbackSamplePosition_ = 0; }

private:
    std::optional<uint32_t> blockCount_;
    std::shared_ptr<VorbisDecodeLoopWorker> worker_;
    uint16_t formatWord_ = kVorbisDecodeFormatWordBank;
    std::vector<uint8_t> outputBuffer_;
    uint64_t playbackSamplePosition_ = 0;
};

// Sec11.1/Sec11.7.2: CAkSrcBankVorbis reads its compressed Vorbis data
// straight out of the loaded bank's own resident memory block - "no stream
// object, no I/O calls anywhere in Bank's own vtable closure." Sec11.9:
// its slot 10 (the decode trigger) is the shared VorbisDecodeTrigger above.
class VorbisBankSource {
public:
    VorbisBankSource(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    const VorbisContainerChunks& header() {
        if (!parsed_) {
            chunks_ = parseRiffWaveContainer(data_, size_);
            parsed_ = true;
        }
        return chunks_;
    }

    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }

    // Sec11.8 HIGH: Bank has no dedicated seek slot at all (vtable stops at
    // index 16) - "seeking" is plausibly absorbed into the shared base-
    // chain's cursor-advance logic (slot 3) as a cheap pointer/offset
    // recompute, since the data is already fully resident. Modelled here
    // only as cursor bookkeeping; no stream state to flush.
    void seek(size_t byteOffsetIntoData) { cursor_ = std::min(byteOffsetIntoData, size_); }
    size_t cursor() const { return cursor_; }

    // Sec11.9: vtable slot 10, the real decode trigger. `decodeState` is
    // this instance's own 184-byte decode-state block (Sec11.3.2's `+0x24`
    // region for Bank) - opaque here, handed straight to the worker.
    VorbisDecodeOutput decodeNextBlock(void* decodeState, uint32_t requestedSamples) {
        return trigger_.decodeNextBlock(decodeState, requestedSamples);
    }
    VorbisDecodeTrigger& decodeTrigger() { return trigger_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t cursor_ = 0;
    VorbisContainerChunks chunks_{};
    bool parsed_ = false;
    VorbisDecodeTrigger trigger_;
};

// Sec11.1/Sec11.6/Sec11.7.2: CAkSrcFileVorbis holds a stream-descriptor
// object pointer and pulls COMPRESSED bytes on demand into a growable
// lookahead buffer (0x00fa3e90's own first half, Sec11.9.1.1: "File also
// keeps its lookahead buffer topped up from its stream object first...
// before reaching the same allocation call" - i.e. this compressed-data
// lookahead is separate from, and filled before, the decode trigger's own
// OUTPUT PCM buffer, Sec11.9.1.1/VorbisDecodeTrigger above).
class VorbisFileSource {
public:
    // Models the per-instance stream-descriptor object's read call (Sec11.6
    // slots 0xc/0x10/0x14/0x3c): pull up to `want` bytes starting at
    // `streamOffset` into `dst`, returning how many were actually
    // available (less than `want` at end-of-stream).
    using PullFn = std::function<size_t(uint8_t* dst, size_t want, uint64_t streamOffset)>;

    explicit VorbisFileSource(PullFn pull) : pull_(std::move(pull)) {}

    // Sec11.7.2: "read more compressed bytes from the stream, reallocating
    // the lookahead region if it's currently too small" - grow-then-pull
    // behaviour only; the original's own wrap-around ring-buffer copy
    // pattern is not replicated.
    void fillLookahead(size_t want) {
        if (lookahead_.size() < want) lookahead_.resize(want);
        lookaheadValid_ = pull_(lookahead_.data(), want, streamOffset_);
    }

    const VorbisContainerChunks& header() {
        if (!parsed_) {
            if (lookaheadValid_ < 12) fillLookahead(std::max<size_t>(lookaheadValid_, size_t{64}));
            chunks_ = parseRiffWaveContainer(lookahead_.data(), lookaheadValid_);
            parsed_ = true;
        }
        return chunks_;
    }

    size_t lookaheadValid() const { return lookaheadValid_; }
    uint64_t streamOffset() const { return streamOffset_; }

    // Sec11.8 HIGH: unlike Bank, File needs its own dedicated seek slot
    // (vtable index 19) because its bytes arrive asynchronously - "flush
    // lookahead, reissue a read at the new offset." The real function's
    // exact correction-term arithmetic (0x00fa36b0) was not traced (OPEN) -
    // this models only the CONFIRMED shape (discard stale lookahead, move
    // the read cursor, force a re-parse on next header()/fillLookahead()).
    void seek(uint64_t newStreamOffset) {
        streamOffset_ = newStreamOffset;
        lookahead_.clear();
        lookaheadValid_ = 0;
        parsed_ = false;
    }

    // Sec11.9: vtable slot 10, the real decode trigger, same shared shape
    // as Bank's (Sec11.9.1: "same two-part shape... parameterised for
    // File's own field offsets"). `decodeState` is this instance's own
    // 184-byte decode-state block (Sec11.3.2's `+0x54` region for File).
    VorbisDecodeOutput decodeNextBlock(void* decodeState, uint32_t requestedSamples) {
        return trigger_.decodeNextBlock(decodeState, requestedSamples);
    }
    VorbisDecodeTrigger& decodeTrigger() { return trigger_; }

private:
    PullFn pull_;
    std::vector<uint8_t> lookahead_;
    size_t lookaheadValid_ = 0;
    uint64_t streamOffset_ = 0;
    VorbisContainerChunks chunks_{};
    bool parsed_ = false;
    VorbisDecodeTrigger trigger_;
};

}  // namespace sr3audioruntime
