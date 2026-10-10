#pragma once

// sr3audioruntime - the audio bring-up task's own decode attempt: wires the
// CONFIRMED container/framing facts of wwise_vorbis_setup.h (spec-audio-
// runtime.md Sec11.11) into the real, vendored libvorbis (third_party/
// libvorbis, BSD-licensed - see that spec's own text: "libvorbis... covers
// that for Team B" for the generic Vorbis-bitstream math) to attempt a real
// decode of a real shipped Wwise-wrapped Vorbis blob.
//
// WHAT THIS FILE USED TO TEST (now a settled, dated NEGATIVE result,
// 2026-10-08 - see wwise_setup_rebuild.h's own header comment for the full
// story): the hypothesis that Wwise's stripped setup packet is byte-for-
// byte identical to what a standard Vorbis decoder's `_vorbis_unpack_books`
// would consume, minus only the standard 7-byte packet-type+signature
// prefix and Ogg's own page/stream framing. tests/real_vorbis_decode_test.cpp
// proved this false against the real population: libvorbis rejects the raw
// stripped packet with OV_EBADHEADER every time. The real bit-level
// transcode this requires now lives in wwise_setup_rebuild.h/.cpp
// (spec-audio-runtime.md Sec11.11.3a) and is called from
// attemptWwiseVorbisDecode() below before the setup packet is ever handed
// to libvorbis - see that file's own header comment for exactly what is
// confirmed, what is this project's own well-evidenced inference, and what
// remains genuinely OPEN (the real population does not yet fully decode).
//
// Reconstructed packets 1 (identification) and 3 (comment) are still built
// directly here from data this project DOES have CONFIRMED (fmt-chunk
// channel count/sample rate, vorb-chunk block-size exponents) using
// libvorbis's own public oggpack_buffer bit-writer, matching
// `_vorbis_unpack_info`/`_vorbis_unpack_comment`'s read order exactly (read
// directly from this project's own vendored third_party/libvorbis/lib/
// info.c - legitimate use of code this project vendored for exactly this
// purpose, not a clean-room concern per this file's own BSD-licensed,
// off-the-shelf footing).
//
// A rejection by `vorbis_synthesis_headerin` (for ANY of the three header
// packets, including the two this project builds itself from CONFIRMED
// data - the id/comment headers are byte-exact, standard, feasible to get
// right, so a failure there would itself be a bug in THIS code, not
// evidence about Wwise's own format) is reported as a concrete, dated,
// precisely-located empirical result - never silently swallowed, never
// re-interpreted as proof of a specific alternate theory. See
// DecodeAttemptResult::stageFailed/libvorbisErrorCode/diagnostic below.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sr3audioruntime {

enum class DecodeStage {
    kContainer,       // RIFF/WAVE/fmt/data/vorb container parse itself
    kIdHeader,        // packet 1 (identification)
    kCommentHeader,   // packet 3 (comment)
    kSetupHeader,     // packet 5 (setup/books) - THE empirical test
    kAudioPackets,    // vorbis_synthesis()/vorbis_synthesis_blockin() loop
    kOk,              // every stage above succeeded
};

struct DecodeAttemptResult {
    DecodeStage stageReached = DecodeStage::kContainer;
    bool ok = false;  // true only if stageReached == kOk
    // libvorbis's own return code for whichever stage first failed (0 if
    // ok, or if the failure was this project's own container-level check
    // rather than a libvorbis call - see diagnostic for that case).
    int libvorbisErrorCode = 0;
    std::string diagnostic;  // human-readable, names the real cause

    // Populated once the id header is accepted (CONFIRMED real values, read
    // back out of libvorbis's own vorbis_info after a successful
    // vorbis_synthesis_headerin() call on packet 1 - not the values this
    // code computed going in, so this also cross-checks the round trip).
    int channels = 0;
    long sampleRate = 0;

    // Populated only if stageReached >= kAudioPackets.
    //
    // UPDATE 2026-10-09 (Wwise modified-packet rebuild task): these two no
    // longer describe a full sweep over every audio packet regardless of
    // failures. Per this task's own explicit instruction ("drop the
    // splice-survivors behavior" - see tests/golden/wwise_vorbis_decode/
    // STATUS.txt and results/verify_vorbis_oracle_comparison_20261008.md for
    // why: silently skipping failed packets and splicing the survivors
    // breaks lapped-MDCT continuity and produced 5 "successes" that were
    // pure noise against an independent oracle), the audio-packet loop now
    // stops at the FIRST packet that fails `vorbis_synthesis()` (or fails
    // this project's own rebuildWwiseAudioPacket() transcode first) and
    // reports failure for the whole decode - it never skips a failing
    // packet and keeps going. So on failure, `audioPacketsTotal` is the
    // count of packets attempted UP TO AND INCLUDING the first failure, and
    // `audioPacketsDecodedOk` is one less than that (every packet before the
    // failure genuinely succeeded) - NOT a sweep over the whole stream.
    // `firstFailedAudioPacketIndex` names exactly which `framed.packets[]`
    // index failed (see real_vorbis_decode.cpp), so a caller/log reader
    // never has to re-derive it from the counts above.
    uint64_t audioPacketsTotal = 0;
    uint64_t audioPacketsDecodedOk = 0;  // vorbis_synthesis() returned 0
    int64_t firstFailedAudioPacketIndex = -1;  // -1 if every attempted packet succeeded

    // Interleaved 16-bit PCM, populated only when ok == true.
    std::vector<int16_t> pcm;
    uint64_t totalSampleFrames = 0;  // per-channel frame count (pcm.size() / channels)
};

// Attempts the real decode described above against one real, already-
// extracted Wwise-wrapped-Vorbis RIFF blob (e.g. one CAkSrcBankVorbis
// record's own bytes, straight out of a real shipped `_media.bnk_pc`
// wrapper's record table - sr3audio::MediaBank - with no Wwise-side
// indirection in between: Sec11.7.2's own "Bank reads its compressed Vorbis
// data straight out of the loaded bank's own resident memory block").
DecodeAttemptResult attemptWwiseVorbisDecode(const uint8_t* riffBlob, size_t riffBlobSize);

// 2026-10-09 (sample-count-trim task). The real exact-sample-count field,
// per spec-audio-runtime.md Sec11.11.2's own field table ("(read
// separately) | 0-3 | loop-point fallback, sample-count-shaped | CONFIRMED
// mechanism; HIGH 'total sample count'") and Sec11.11.5 ("loopEnd =
// vorbChunk.firstDword - 1 - the dword Sec11.11.2 noted was excluded from
// the main 38-byte copy and read back separately"): the "vorb" chunk's own
// FIRST dword (`WwiseVorbChunk::sampleCountFallbackField`, raw42[0..3]),
// deliberately excluded from the 38-byte packet-framing/setup copy this
// project already consumes elsewhere (vorb[4:8)/[8:12)/[16:20)/[20:24)/
// [36:40) and bytes 40/41 - see wwise_setup_rebuild.h's own header comment
// and real_vorbis_decode.cpp's attemptWwiseVorbisDecode() for what those
// are already used for). wwise_vorbis_setup.h's own header comment
// independently corroborates the "total sample count" reading: "matches a
// plausible real duration in every sample checked (e.g. 2,228,448 at 48 kHz
// mono = 46.4s, consistent with that record's own compressed size)."
//
// Units are PER-CHANNEL FRAMES - the same convention DecodeAttemptResult::
// totalSampleFrames already uses (`pcm.size() / channels`), and the same
// convention the public Vorbis granule-position field and libvorbis's own
// `vorbis_synthesis_pcmout()` "samples" return value use throughout this
// file's decode loop (a per-channel frame count, never a total-interleaved-
// sample count) - not independently distinguishable from a real sample yet
// (every one of this project's 5 currently-decoding oracle records is
// mono), but the only reading consistent with how every other sample-count
// quantity in this codebase is already expressed.
//
// Trims `pcm` (interleaved, `channels`-wide) down to AT MOST
// `vorbSampleCountField` frames - NEVER pads/extends: a decode producing
// FEWER frames than the field claims is a different, out-of-scope bug, and
// padding with invented samples would itself be exactly the kind of
// fabrication this project's "no invented fixes" rule forbids. No-op if
// `channels<=0`, `vorbSampleCountField==0` (a 0 field is not itself CONFIRMED
// to mean "no real record," but trimming to 0 frames would silently delete
// an entire real decode on what would then be a mis-read field - the safer,
// non-destructive choice), or `vorbSampleCountField >= currentFrameCount`
// (nothing to trim). Returns the resulting frame count (unchanged from
// `currentFrameCount` on every no-op path above).
uint64_t trimPcmFramesToSampleCount(std::vector<int16_t>& pcm, int channels, uint64_t currentFrameCount,
                                     uint64_t vorbSampleCountField);

}  // namespace sr3audioruntime
