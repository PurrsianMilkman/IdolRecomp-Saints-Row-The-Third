// Synthetic tests for the audio bring-up task's own additions:
// include/sr3audioruntime/wwise_vorbis_setup.h (the Wwise-specific Vorbis
// wrapping layer, spec-audio-runtime.md Sec11.11, unit U-W4c) and
// include/sr3audioruntime/wav_writer.h (this project's first audio output
// writer). Every test cites the spec section or the real-data measurement
// it exercises, following this project's own synthetic-test convention
// (see tests/synthetic_audio_runtime_test.cpp's own header comment).
//
// attemptWwiseVorbisDecode() itself (include/sr3audioruntime/
// real_vorbis_decode.h) is NOT exercised here with a real decode success:
// its one genuinely interesting question - whether the real Wwise stripped
// setup packet is byte-compatible with a standard Vorbis setup header minus
// framing - can only be answered against REAL shipped Wwise Vorbis data
// (this project does not have a from-scratch Vorbis encoder to fabricate a
// synthetic one), which is exactly what tests/real_vorbis_decode_test.cpp
// (gated on CRREISH_GAME_CACHE_DIR) does. What IS fully testable here,
// synthetically, is every CONTAINER-LEVEL step this project's own code
// controls end to end: chunk location, the corrected vorb-chunk field
// positions, fmt decoding, packet framing, and the WAV writer.

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sr3audioruntime/real_vorbis_decode.h"
#include "sr3audioruntime/wav_writer.h"
#include "sr3audioruntime/wwise_setup_rebuild.h"
#include "sr3audioruntime/wwise_vorbis_setup.h"

using namespace sr3audioruntime;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)

void putTag(std::vector<uint8_t>& buf, const char* fourcc) {
    for (int i = 0; i < 4; ++i) buf.push_back(static_cast<uint8_t>(fourcc[i]));
}
void putU32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xff));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}
void putU16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xff));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
}

// Builds a synthetic RIFF/WAVE/fmt/vorb/data buffer shaped exactly like a
// real Wwise-Vorbis-wrapped record (spec-audio-runtime.md Sec11.11.1/
// Sec11.11.2): a 24-byte extended "fmt " chunk, a 42-byte "vorb" chunk with
// the sample-count fallback at bytes 0-3 and the block-size exponents at
// bytes 40/41 (this project's own 2026-10-08 real-data-corrected
// positions - see wwise_vorbis_setup.h's top-of-file note), and a "data"
// chunk holding one or more 16-bit-length-prefixed framed packets
// (Sec11.11.1).
std::vector<uint8_t> buildWwiseVorbisRiff(uint16_t channels, uint32_t sampleRate, uint8_t bs0exp, uint8_t bs1exp,
                                            uint32_t sampleCountFallback,
                                            const std::vector<std::vector<uint8_t>>& framedPackets) {
    std::vector<uint8_t> fmtBody;
    putU16(fmtBody, 0xFFFF);  // Wwise's own non-standard formatTag, CONFIRMED real-data shape
    putU16(fmtBody, channels);
    putU32(fmtBody, sampleRate);
    putU32(fmtBody, 0);  // avgBytesPerSec, unused by this project's reader
    putU16(fmtBody, 0);  // blockAlign, unused
    putU16(fmtBody, 0);  // bitsPerSample, unused
    putU16(fmtBody, 4);  // cbSize
    putU32(fmtBody, 0);  // 4 extra bytes, unused

    std::vector<uint8_t> vorbBody(42, 0);
    vorbBody[0] = static_cast<uint8_t>(sampleCountFallback & 0xff);
    vorbBody[1] = static_cast<uint8_t>((sampleCountFallback >> 8) & 0xff);
    vorbBody[2] = static_cast<uint8_t>((sampleCountFallback >> 16) & 0xff);
    vorbBody[3] = static_cast<uint8_t>((sampleCountFallback >> 24) & 0xff);
    vorbBody[40] = bs0exp;
    vorbBody[41] = bs1exp;

    std::vector<uint8_t> dataBody;
    for (const auto& pkt : framedPackets) {
        dataBody.push_back(static_cast<uint8_t>(pkt.size() & 0xff));
        dataBody.push_back(static_cast<uint8_t>((pkt.size() >> 8) & 0xff));
        dataBody.insert(dataBody.end(), pkt.begin(), pkt.end());
    }

    std::vector<uint8_t> buf;
    putTag(buf, "RIFF");
    putU32(buf, 0);  // overall size, not checked by this project's reader
    putTag(buf, "WAVE");

    putTag(buf, "fmt ");
    putU32(buf, static_cast<uint32_t>(fmtBody.size()));
    buf.insert(buf.end(), fmtBody.begin(), fmtBody.end());

    putTag(buf, "vorb");
    putU32(buf, static_cast<uint32_t>(vorbBody.size()));
    buf.insert(buf.end(), vorbBody.begin(), vorbBody.end());

    putTag(buf, "data");
    putU32(buf, static_cast<uint32_t>(dataBody.size()));
    buf.insert(buf.end(), dataBody.begin(), dataBody.end());

    return buf;
}

// ---------------------------------------------------------------------
// findRiffSubChunk(): the generic caller-chosen-FourCC locator this file
// adds on top of vorbis_container.h's own fixed four-tag walker.
// ---------------------------------------------------------------------
void testFindRiffSubChunk_LocatesVorbAndRejectsMissingOrInvalid() {
    std::vector<uint8_t> buf =
        buildWwiseVorbisRiff(/*channels*/ 1, /*rate*/ 48000, /*bs0exp*/ 8, /*bs1exp*/ 11,
                              /*sampleCountFallback*/ 2228448, {{1, 2, 3, 4}});
    auto loc = findRiffSubChunk(buf.data(), buf.size(), kFourCcVorb);
    CHECK(loc.has_value());
    CHECK(loc->second == 42u);  // the CONFIRMED exact 42-byte "vorb" chunk size

    // A tag that genuinely isn't present.
    constexpr uint32_t kFourCcZzzz = 0x7a7a7a7a;  // "zzzz"
    CHECK(!findRiffSubChunk(buf.data(), buf.size(), kFourCcZzzz).has_value());

    // Not RIFF/WAVE at all.
    std::vector<uint8_t> garbage = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    CHECK(!findRiffSubChunk(garbage.data(), garbage.size(), kFourCcVorb).has_value());

    // Buffer too short to even hold a RIFF/WAVE header.
    std::vector<uint8_t> tiny = {'R', 'I', 'F', 'F'};
    CHECK(!findRiffSubChunk(tiny.data(), tiny.size(), kFourCcVorb).has_value());
}

// ---------------------------------------------------------------------
// parseVorbChunk(): the corrected byte positions (2026-10-08 real-data
// measurement) - raw42[0..3] sample-count fallback, raw42[40]/raw42[41]
// block-size exponents, NOT spec-audio-runtime.md Sec11.11.2's literal
// "byte 4/5" (see wwise_vorbis_setup.h's own top-of-file correction note
// for the 800-real-sample evidence this is based on).
// ---------------------------------------------------------------------
void testParseVorbChunk_CorrectedBytePositions() {
    std::vector<uint8_t> buf =
        buildWwiseVorbisRiff(1, 48000, /*bs0exp*/ 8, /*bs1exp*/ 11, /*sampleCountFallback*/ 2228448, {{1, 2, 3, 4}});
    auto loc = findRiffSubChunk(buf.data(), buf.size(), kFourCcVorb);
    CHECK(loc.has_value());
    WwiseVorbChunk vorb = parseVorbChunk(buf.data(), buf.size(), loc->first);
    CHECK(vorb.present);
    CHECK(vorb.raw42.size() == 42u);
    CHECK(vorb.sampleCountFallbackField == 2228448u);
    CHECK(vorb.blockSizeExponent0 == 8);
    CHECK(vorb.blockSizeExponent1 == 11);
    CHECK(vorb.blockSize0 == 256u);   // 1 << 8
    CHECK(vorb.blockSize1 == 2048u);  // 1 << 11
    CHECK(vorb.blockSizesWithinVorbisSpecRange());

    // The real data this project measured NEVER has anything legal at the
    // OLD (wrong) byte 4/5 position - a synthetic buffer built the old way
    // (exponents at [4]/[5], nothing meaningful at [40]/[41]) must fail the
    // legal-range check, proving this reader is reading [40]/[41] and NOT
    // [4]/[5].
    std::vector<uint8_t> oldPositionBuf = buf;  // copy, then corrupt [40]/[41], populate [4]/[5] instead
    auto loc2 = findRiffSubChunk(oldPositionBuf.data(), oldPositionBuf.size(), kFourCcVorb);
    CHECK(loc2.has_value());
    oldPositionBuf[loc2->first + 4] = 8;
    oldPositionBuf[loc2->first + 5] = 11;
    oldPositionBuf[loc2->first + 40] = 0;  // not a legal exponent (1<<0 == 1 < 64)
    oldPositionBuf[loc2->first + 41] = 0;
    WwiseVorbChunk vorbOldPos = parseVorbChunk(oldPositionBuf.data(), oldPositionBuf.size(), loc2->first);
    CHECK(vorbOldPos.present);
    CHECK(!vorbOldPos.blockSizesWithinVorbisSpecRange());  // proves this reader ignores byte 4/5 entirely

    // Out-of-range input: vorbChunkOffset + 42 doesn't fit in the buffer.
    WwiseVorbChunk shortChunk = parseVorbChunk(buf.data(), buf.size(), buf.size() - 10);
    CHECK(!shortChunk.present);

    // Safety: an exponent byte of 255 (not real data, but a malformed/
    // garbage input this function must not crash on - no CONFIRMED rule
    // says a "vorb" chunk can't carry garbage) must not invoke undefined
    // shift-by->=32 behaviour, and must still fail the legal-range check.
    std::vector<uint8_t> extremeBuf = buf;
    auto loc3 = findRiffSubChunk(extremeBuf.data(), extremeBuf.size(), kFourCcVorb);
    CHECK(loc3.has_value());
    extremeBuf[loc3->first + 40] = 255;
    extremeBuf[loc3->first + 41] = 255;
    WwiseVorbChunk extreme = parseVorbChunk(extremeBuf.data(), extremeBuf.size(), loc3->first);
    CHECK(extreme.present);
    CHECK(extreme.blockSizeExponent0 == 255);
    CHECK(extreme.blockSize0 == 0u);  // guarded, not UB
    CHECK(!extreme.blockSizesWithinVorbisSpecRange());
}

// ---------------------------------------------------------------------
// parseStandardWaveFmt(): the ordinary, public WAVEFORMATEX fields this
// project needs (channels/sampleRate), independent of Wwise's own
// formatTag==0xFFFF extension (public file-format knowledge, same footing
// as vorbis_container.h's own RIFF framing).
// ---------------------------------------------------------------------
void testParseStandardWaveFmt_DecodesChannelsAndRate() {
    std::vector<uint8_t> buf = buildWwiseVorbisRiff(2, 44100, 8, 11, 12345, {{9, 9}});
    VorbisContainerChunks chunks = parseRiffWaveContainer(buf.data(), buf.size());
    CHECK(chunks.valid && chunks.hasFmt);
    StandardWaveFmt fmt = parseStandardWaveFmt(buf.data(), chunks.fmtOffset, chunks.fmtSize);
    CHECK(fmt.present);
    CHECK(fmt.channels == 2);
    CHECK(fmt.samplesPerSec == 44100u);

    // Too short to hold even the base 16 bytes.
    StandardWaveFmt tooShort = parseStandardWaveFmt(buf.data(), chunks.fmtOffset, 8);
    CHECK(!tooShort.present);
}

// ---------------------------------------------------------------------
// walkFramedPackets(): Sec11.11.1's bare 16-bit length-prefixed framing,
// no Ogg pages - both the well-formed and the truncated/malformed cases.
// ---------------------------------------------------------------------
void testWalkFramedPackets_WellFormedAndTruncated() {
    std::vector<std::vector<uint8_t>> packets = {{1, 2, 3}, {4, 5, 6, 7, 8}, {9}};
    std::vector<uint8_t> buf = buildWwiseVorbisRiff(1, 48000, 8, 11, 100, packets);
    VorbisContainerChunks chunks = parseRiffWaveContainer(buf.data(), buf.size());
    CHECK(chunks.valid && chunks.hasData);

    FramedPacketWalkResult walk = walkFramedPackets(buf.data(), chunks.dataOffset, chunks.dataSize);
    CHECK(walk.consistent);
    CHECK(walk.packets.size() == 3u);
    CHECK(walk.packets[0].length == 3u);
    CHECK(walk.packets[1].length == 5u);
    CHECK(walk.packets[2].length == 1u);
    // Offsets are absolute into the SAME buffer passed in, not re-based to 0.
    CHECK(buf[walk.packets[0].offset] == 1);
    CHECK(buf[walk.packets[1].offset] == 4);
    CHECK(buf[walk.packets[2].offset] == 9);

    // Truncated: a declared length reaching past the data chunk's own end.
    std::vector<uint8_t> truncated = {0x05, 0x00, 1, 2};  // declares 5 bytes, only 2 present
    FramedPacketWalkResult badWalk = walkFramedPackets(truncated.data(), 0, truncated.size());
    CHECK(!badWalk.consistent);
    CHECK(badWalk.packets.empty());

    // A dangling 1-byte length prefix (can't even read the 2-byte prefix).
    std::vector<uint8_t> dangling = {0x03, 0x00, 1, 2, 3, 0xAA};
    FramedPacketWalkResult danglingWalk = walkFramedPackets(dangling.data(), 0, dangling.size());
    CHECK(!danglingWalk.consistent);
    CHECK(danglingWalk.packets.size() == 1u);  // the one well-formed packet before the dangling byte
}

// ---------------------------------------------------------------------
// attemptWwiseVorbisDecode(): container-level rejection paths this
// project's own code controls end to end (no real libvorbis setup-packet
// dependency - see this file's own header comment for why a synthetic
// SUCCESS case isn't attempted here).
// ---------------------------------------------------------------------
void testAttemptDecode_ContainerLevelRejections() {
    // Not RIFF/WAVE at all.
    std::vector<uint8_t> garbage = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
    DecodeAttemptResult r1 = attemptWwiseVorbisDecode(garbage.data(), garbage.size());
    CHECK(r1.stageReached == DecodeStage::kContainer);
    CHECK(!r1.ok);

    // Valid RIFF/WAVE/fmt/data but no "vorb" chunk at all.
    std::vector<uint8_t> noVorb;
    putTag(noVorb, "RIFF");
    putU32(noVorb, 0);
    putTag(noVorb, "WAVE");
    putTag(noVorb, "fmt ");
    putU32(noVorb, 16);
    noVorb.resize(noVorb.size() + 16, 0);
    putTag(noVorb, "data");
    putU32(noVorb, 4);
    noVorb.insert(noVorb.end(), {1, 2, 3, 4});
    DecodeAttemptResult r2 = attemptWwiseVorbisDecode(noVorb.data(), noVorb.size());
    CHECK(r2.stageReached == DecodeStage::kContainer);
    CHECK(!r2.ok);

    // "vorb" present but block-size exponents out of the legal Vorbis I
    // range (64 <= blocksize0 <= blocksize1 <= 8192) - this project's own
    // measure-don't-gate check correctly refuses to proceed.
    std::vector<uint8_t> illegalBlockSize =
        buildWwiseVorbisRiff(1, 48000, /*bs0exp*/ 20, /*bs1exp*/ 20, 100, {{1, 2, 3}});
    DecodeAttemptResult r3 = attemptWwiseVorbisDecode(illegalBlockSize.data(), illegalBlockSize.size());
    CHECK(r3.stageReached == DecodeStage::kContainer);
    CHECK(!r3.ok);
    CHECK(r3.diagnostic.find("legal range") != std::string::npos);

    // "vorb" present, legal block sizes, but the "data" chunk's framing
    // walk produces zero packets (an empty data chunk).
    std::vector<uint8_t> emptyData = buildWwiseVorbisRiff(1, 48000, 8, 11, 100, {});
    DecodeAttemptResult r4 = attemptWwiseVorbisDecode(emptyData.data(), emptyData.size());
    CHECK(r4.stageReached == DecodeStage::kContainer);
    CHECK(!r4.ok);
}

// ---------------------------------------------------------------------
// trimPcmFramesToSampleCount() (real_vorbis_decode.h, 2026-10-09
// sample-count-trim task): spec-audio-runtime.md Sec11.11.2's own field
// table ("(read separately) | 0-3 | loop-point fallback, sample-count-
// shaped | CONFIRMED mechanism; HIGH 'total sample count'") plus
// Sec11.11.5's "loopEnd = vorbChunk.firstDword - 1" text identify the
// "vorb" chunk's own first dword (WwiseVorbChunk::sampleCountFallbackField)
// as the record's real exact-sample-count field - the fix for the small
// "~0.01-0.02s longer than the oracle" residual results/verify_vorbis_
// oracle_comparison_20261009_post_packet_rebuild_fix.md flagged (an
// un-trimmed lapped-MDCT final-block ring-out). This pure helper is tested
// standalone, per this project's own "synthetically test every step this
// code controls end to end" convention (this file's own header comment) -
// fabricating a full real Vorbis-decode-success path synthetically is not
// practical (same reasoning that file's header comment already gives for
// why attemptWwiseVorbisDecode()'s own real-decode path is only exercised
// against real data in tests/real_vorbis_decode_test.cpp).
// ---------------------------------------------------------------------

void testTrimPcmFramesToSampleCount_TrimsDownNeverPadsOrNoOps() {
    // This task's own worked example: decode produces N=10 frames, the
    // vorb field says M=7 < N - assert the output is trimmed to EXACTLY M
    // frames (interleaved stereo, so 2 int16 per frame).
    {
        std::vector<int16_t> pcm;
        for (int frame = 0; frame < 10; ++frame) {
            pcm.push_back(static_cast<int16_t>(frame * 2));      // left
            pcm.push_back(static_cast<int16_t>(frame * 2 + 1));  // right
        }
        uint64_t result = trimPcmFramesToSampleCount(pcm, /*channels=*/2, /*currentFrameCount=*/10,
                                                       /*vorbSampleCountField=*/7);
        CHECK(result == 7);
        CHECK(pcm.size() == 14);  // 7 frames * 2 channels
        // The trim keeps the LEADING frames unchanged (discards the
        // trailing ring-out, never reorders/rewrites surviving samples).
        for (int frame = 0; frame < 7; ++frame) {
            CHECK(pcm[frame * 2] == static_cast<int16_t>(frame * 2));
            CHECK(pcm[frame * 2 + 1] == static_cast<int16_t>(frame * 2 + 1));
        }
    }

    // Mono case (every one of this project's 5 currently-decoding real
    // oracle records is mono) - 1 int16 per frame, same trim-down shape.
    {
        std::vector<int16_t> pcm(10, 0);
        for (int i = 0; i < 10; ++i) pcm[i] = static_cast<int16_t>(i);
        uint64_t result = trimPcmFramesToSampleCount(pcm, /*channels=*/1, /*currentFrameCount=*/10,
                                                       /*vorbSampleCountField=*/4);
        CHECK(result == 4);
        CHECK(pcm.size() == 4);
        CHECK(pcm[0] == 0 && pcm[1] == 1 && pcm[2] == 2 && pcm[3] == 3);
    }

    // Never PADS: field says M >= N (more frames than we actually
    // decoded) - a different, out-of-scope bug if it ever happens for
    // real, not something this helper invents samples to paper over.
    {
        std::vector<int16_t> pcm = {1, 2, 3, 4};  // 4 mono frames
        uint64_t result = trimPcmFramesToSampleCount(pcm, 1, 4, /*vorbSampleCountField=*/4);
        CHECK(result == 4);
        CHECK(pcm.size() == 4);

        std::vector<int16_t> pcm2 = {1, 2, 3, 4};
        uint64_t result2 = trimPcmFramesToSampleCount(pcm2, 1, 4, /*vorbSampleCountField=*/100);
        CHECK(result2 == 4);
        CHECK(pcm2.size() == 4);
    }

    // vorbSampleCountField == 0: deliberately a no-op (see real_vorbis_
    // decode.h's own header comment - a 0 field is treated as "not a real
    // trim target" rather than destructively trimming a real decode down
    // to silence on what could just as easily be a mis-read field).
    {
        std::vector<int16_t> pcm = {1, 2, 3, 4, 5, 6};
        uint64_t result = trimPcmFramesToSampleCount(pcm, 1, 6, /*vorbSampleCountField=*/0);
        CHECK(result == 6);
        CHECK(pcm.size() == 6);
    }

    // channels <= 0: also a no-op (can't be interpreted as a frame count
    // without a channel width).
    {
        std::vector<int16_t> pcm = {1, 2, 3, 4};
        uint64_t result = trimPcmFramesToSampleCount(pcm, 0, 4, /*vorbSampleCountField=*/2);
        CHECK(result == 4);
        CHECK(pcm.size() == 4);
    }
}

// ---------------------------------------------------------------------
// wav_writer.h: writeWavPcm16()/wavDurationSeconds() round trip - this
// project's first audio output writer, same "single free function, no
// class" shape as src/png_writer.cpp's own writePng() (see wav_writer.h's
// own header comment).
// ---------------------------------------------------------------------
void testWavWriter_RoundTripDurationAndHeaderShape() {
    const uint32_t sampleRate = 48000;
    const uint16_t channels = 2;
    const uint32_t frames = 4800;  // exactly 0.1s at 48kHz
    std::vector<int16_t> pcm(static_cast<size_t>(frames) * channels);
    for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<int16_t>((i % 2 == 0) ? 1000 : -1000);

    const std::string path = "synthetic_wav_writer_roundtrip_test.wav";
    CHECK(writeWavPcm16(path, pcm, sampleRate, channels));

    bool ok = false;
    double duration = wavDurationSeconds(path, ok);
    CHECK(ok);
    CHECK(duration > 0.0999 && duration < 0.1001);  // 4800 frames / 48000 Hz == 0.1s exactly

    // Read the raw bytes back and check the canonical 44-byte header shape
    // this writer's own header comment promises (one fmt + one data
    // sub-chunk, PCM format tag 1).
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(raw.size() == 44 + pcm.size() * sizeof(int16_t));
    CHECK(raw[0] == 'R' && raw[1] == 'I' && raw[2] == 'F' && raw[3] == 'F');
    CHECK(raw[8] == 'W' && raw[9] == 'A' && raw[10] == 'V' && raw[11] == 'E');
    CHECK(raw[12] == 'f' && raw[13] == 'm' && raw[14] == 't' && raw[15] == ' ');
    uint32_t fmtSize = raw[16] | (raw[17] << 8) | (raw[18] << 16) | (raw[19] << 24);
    CHECK(fmtSize == 16u);
    uint16_t formatTag = static_cast<uint16_t>(raw[20] | (raw[21] << 8));
    CHECK(formatTag == 1u);  // PCM
    CHECK(raw[36] == 'd' && raw[37] == 'a' && raw[38] == 't' && raw[39] == 'a');

    // Zero channels must fail cleanly (no file written), never crash/divide
    // by zero.
    CHECK(!writeWavPcm16("synthetic_wav_writer_zero_channels_test.wav", pcm, sampleRate, 0));

    // An empty PCM buffer (0 duration) still produces a valid, readable
    // header - wavDurationSeconds() returns exactly 0.
    const std::string emptyPath = "synthetic_wav_writer_empty_test.wav";
    CHECK(writeWavPcm16(emptyPath, {}, sampleRate, 1));
    bool emptyOk = false;
    double emptyDuration = wavDurationSeconds(emptyPath, emptyOk);
    CHECK(emptyOk);
    CHECK(emptyDuration == 0.0);

    // A file that isn't a WAV at all is rejected, not mis-parsed.
    bool garbageOk = true;
    const std::string garbagePath = "synthetic_wav_writer_garbage_test.wav";
    {
        std::ofstream g(garbagePath, std::ios::binary | std::ios::trunc);
        g << "not a wav file, too short anyway";
    }
    double garbageDuration = wavDurationSeconds(garbagePath, garbageOk);
    CHECK(!garbageOk);
    CHECK(garbageDuration == 0.0);
}

// ---------------------------------------------------------------------
// rebuildWwiseAudioPacket() (wwise_setup_rebuild.h, spec-audio-runtime.md
// Sec11.11.4a): hand-constructed raw-bit fixtures, independent of libogg's
// own oggpack_buffer (this project's own vendored, non-exported-internal
// bit packer - linking it into this test target isn't needed for what this
// file already tests). Packs/unpacks bits LSB-first within each byte,
// verified directly against third_party/libogg/src/bitwise.c's own
// `oggpack_write()` (`value&=mask[bits]; ... b->ptr[0]|=value<<b->endbit`,
// i.e. value's bit 0 is the FIRST bit appended to the stream) - the exact
// same convention rebuildWwiseAudioPacket() itself reads/writes through
// real libogg, so a fixture built this way and real production code agree
// bit-for-bit on what "bit 0 of the stream" means.
struct BitWriter {
    std::vector<uint8_t> bytes;
    int bitPos = 0;  // next bit to fill within bytes.back(), 0-7

    void writeBits(unsigned long value, int n) {
        for (int i = 0; i < n; ++i) {
            if (bitPos == 0) bytes.push_back(0);
            const unsigned bit = static_cast<unsigned>((value >> i) & 1u);
            bytes.back() = static_cast<uint8_t>(bytes.back() | (bit << bitPos));
            bitPos = (bitPos + 1) % 8;
        }
    }
};

struct BitReader {
    const std::vector<uint8_t>& bytes;
    size_t bitPos = 0;
    explicit BitReader(const std::vector<uint8_t>& b) : bytes(b) {}
    unsigned long readBits(int n) {
        unsigned long v = 0;
        for (int i = 0; i < n; ++i) {
            const size_t byteIdx = bitPos / 8;
            const int bitIdx = static_cast<int>(bitPos % 8);
            const unsigned bit = byteIdx < bytes.size() ? ((bytes[byteIdx] >> bitIdx) & 1u) : 0u;
            v |= (static_cast<unsigned long>(bit) << i);
            ++bitPos;
        }
        return v;
    }
};

// Builds one raw Wwise "modified packet" audio packet: the single hardcoded
// mode-selector bit (Sec11.11.4a), then `payloadBits` more bits of known
// content (the "rest of the packet" rebuildWwiseAudioPacket() must copy
// through unchanged) - padded to a whole byte with zero bits, same as any
// real byte-sized packet.
std::vector<uint8_t> buildRawWwiseAudioPacket(unsigned modeBit, unsigned long payloadValue, int payloadBits) {
    BitWriter w;
    w.writeBits(modeBit, 1);
    w.writeBits(payloadValue, payloadBits);
    return w.bytes;
}

void testRebuildWwiseAudioPacket_ShortBlock_PrefixShapeAndPayloadPassthrough() {
    // mode 0 = short block (Sec11.11.4a: no window-flag bits for a short
    // block's mode, same as the public Vorbis I spec's own grammar).
    const std::vector<bool> modeBlockflags = {false, true};
    const unsigned long payload = 0b1011001;  // 7 bits, arbitrary known pattern
    std::vector<uint8_t> raw = buildRawWwiseAudioPacket(/*modeBit*/ 0, payload, 7);

    AudioPacketRebuildResult r = rebuildWwiseAudioPacket(raw.data(), raw.size(), /*hasPreviousPacket=*/false,
                                                          /*previousPacketIsLong=*/false, /*hasNextPacket=*/false,
                                                          /*nextPacketIsLong=*/false, modeBlockflags);
    CHECK(r.ok);
    BitReader br(r.standardPacket);
    CHECK(br.readBits(1) == 0);  // inserted packet-type bit (standard Vorbis, always 0=audio)
    CHECK(br.readBits(1) == 0);  // mode number, 1 bit (CONFIRMED width) - same value as the raw packet's own
    // Short block: NO window-flag bits inserted - the rest of the original
    // packet's own bits (the 7-bit payload) follow immediately.
    CHECK(br.readBits(7) == payload);
}

// 2026-10-09 CORRECTION (this task's own first job): this test used to pass
// `prev`/`next` raw packet BYTES straight into rebuildWwiseAudioPacket(),
// which internally read their mode bits itself - exactly the "peek at a
// neighboring packet's own raw bytes" mechanism spec-audio-runtime.md
// Sec11.11.4a says the real exe never does. rebuildWwiseAudioPacket() no
// longer accepts neighbor bytes at all (see its own header comment); this
// test now resolves each neighbor's own mode bit via
// resolveWwiseAudioModeBit() FIRST - exactly as a real caller's sequential,
// turn-respecting loop would, one packet at a time - and passes only the
// resulting booleans through.
void testRebuildWwiseAudioPacket_LongBlock_WindowFlagsFromNeighborModes() {
    // mode 0 = short, mode 1 = long.
    const std::vector<bool> modeBlockflags = {false, true};
    const unsigned long payload = 0b101101010110;  // 12 bits, crosses a byte boundary
    std::vector<uint8_t> raw = buildRawWwiseAudioPacket(/*modeBit*/ 1, payload, 12);

    std::vector<uint8_t> prev = buildRawWwiseAudioPacket(/*modeBit*/ 1, 0, 3);  // previous packet: LONG mode
    std::vector<uint8_t> next = buildRawWwiseAudioPacket(/*modeBit*/ 0, 0, 3);  // next packet: SHORT mode
    AudioModeBitResolution prevMode = resolveWwiseAudioModeBit(prev.data(), prev.size(), modeBlockflags);
    AudioModeBitResolution nextMode = resolveWwiseAudioModeBit(next.data(), next.size(), modeBlockflags);
    CHECK(prevMode.ok && prevMode.isLong);
    CHECK(nextMode.ok && !nextMode.isLong);

    AudioPacketRebuildResult r = rebuildWwiseAudioPacket(raw.data(), raw.size(), /*hasPreviousPacket=*/true,
                                                          prevMode.isLong, /*hasNextPacket=*/true, nextMode.isLong,
                                                          modeBlockflags);
    CHECK(r.ok);
    BitReader br(r.standardPacket);
    CHECK(br.readBits(1) == 0);  // packet type
    CHECK(br.readBits(1) == 1);  // mode number == 1 (long)
    CHECK(br.readBits(1) == 1);  // previousWindowFlag: previous packet's own mode (1) is long
    CHECK(br.readBits(1) == 0);  // nextWindowFlag: next packet's own mode (0) is short
    CHECK(br.readBits(12) == payload);
}

void testRebuildWwiseAudioPacket_StreamBoundaries_NoNeighborMeansShortWindowFlag() {
    // Long-block mode, but no previous/next packet at all (the first real
    // audio packet's own "no prior content" case, and the stream's last
    // packet's own "no following content" case) - this function's own
    // documented convention: treated as if the missing neighbor were short
    // (window flag 0), not guessed/invented, explicitly chosen per this
    // function's own header comment.
    const std::vector<bool> modeBlockflags = {false, true};
    std::vector<uint8_t> raw = buildRawWwiseAudioPacket(/*modeBit*/ 1, 0b101, 3);

    AudioPacketRebuildResult r = rebuildWwiseAudioPacket(raw.data(), raw.size(), /*hasPreviousPacket=*/false,
                                                          /*previousPacketIsLong=*/false, /*hasNextPacket=*/false,
                                                          /*nextPacketIsLong=*/false, modeBlockflags);
    CHECK(r.ok);
    BitReader br(r.standardPacket);
    CHECK(br.readBits(1) == 0);
    CHECK(br.readBits(1) == 1);
    CHECK(br.readBits(1) == 0);  // previousWindowFlag: no previous packet -> 0
    CHECK(br.readBits(1) == 0);  // nextWindowFlag: no next packet -> 0
    CHECK(br.readBits(3) == 0b101u);
}

// `previousPacketIsLong`/`nextPacketIsLong` must be ignored whenever the
// corresponding `has*Packet` is false - a caller (real_vorbis_decode.cpp's
// own loop) passes whatever stale/default boolean it happens to have lying
// around for "isLong" at a stream boundary where "has" is false; this
// function must not trust that stale value.
void testRebuildWwiseAudioPacket_IgnoresIsLongWhenHasFlagIsFalse() {
    const std::vector<bool> modeBlockflags = {false, true};
    std::vector<uint8_t> raw = buildRawWwiseAudioPacket(/*modeBit*/ 1, 0b11, 2);

    AudioPacketRebuildResult r = rebuildWwiseAudioPacket(raw.data(), raw.size(), /*hasPreviousPacket=*/false,
                                                          /*previousPacketIsLong=*/true,  // deliberately "wrong"
                                                          /*hasNextPacket=*/false,
                                                          /*nextPacketIsLong=*/true,  // deliberately "wrong"
                                                          modeBlockflags);
    CHECK(r.ok);
    BitReader br(r.standardPacket);
    CHECK(br.readBits(1) == 0);
    CHECK(br.readBits(1) == 1);
    CHECK(br.readBits(1) == 0);  // previousWindowFlag still 0 - hasPreviousPacket=false wins
    CHECK(br.readBits(1) == 0);  // nextWindowFlag still 0 - hasNextPacket=false wins
}

void testRebuildWwiseAudioPacket_RejectsEmptyInputAndOutOfRangeModeBit() {
    const std::vector<bool> modeBlockflags = {false, true};

    // Null/empty raw packet.
    AudioPacketRebuildResult rEmpty =
        rebuildWwiseAudioPacket(nullptr, 0, false, false, false, false, modeBlockflags);
    CHECK(!rEmpty.ok);

    // Empty modeBlockflags (setup header's own mode list never parsed).
    std::vector<uint8_t> raw = buildRawWwiseAudioPacket(0, 0, 3);
    AudioPacketRebuildResult rNoModes =
        rebuildWwiseAudioPacket(raw.data(), raw.size(), false, false, false, false, {});
    CHECK(!rNoModes.ok);

    // Mode-selector bit value (1) addresses a mode beyond a 1-entry
    // modeBlockflags list - Sec11.11.4a's own 2-live-mode structural cap
    // does not hold for this (synthetic, deliberately malformed) record.
    std::vector<uint8_t> rawOutOfRange = buildRawWwiseAudioPacket(/*modeBit*/ 1, 0, 3);
    AudioPacketRebuildResult rOutOfRange =
        rebuildWwiseAudioPacket(rawOutOfRange.data(), rawOutOfRange.size(), false, false, false, false, {false});
    CHECK(!rOutOfRange.ok);
    CHECK(rOutOfRange.diagnostic.find("beyond") != std::string::npos);
}

// ---------------------------------------------------------------------
// resolveWwiseAudioModeBit() (wwise_setup_rebuild.h): direct coverage, now
// that it is a standalone public function (split out of
// rebuildWwiseAudioPacket()'s own prior internal `peekModeBit()` helper -
// see this file's own 2026-10-09 CORRECTION comment above).
void testResolveWwiseAudioModeBit_ShortAndLongAndErrors() {
    const std::vector<bool> modeBlockflags = {false, true};  // mode 0 = short, mode 1 = long

    std::vector<uint8_t> shortPkt = buildRawWwiseAudioPacket(/*modeBit*/ 0, 0b11, 2);
    AudioModeBitResolution shortRes = resolveWwiseAudioModeBit(shortPkt.data(), shortPkt.size(), modeBlockflags);
    CHECK(shortRes.ok);
    CHECK(shortRes.modeBit == 0);
    CHECK(!shortRes.isLong);

    std::vector<uint8_t> longPkt = buildRawWwiseAudioPacket(/*modeBit*/ 1, 0b11, 2);
    AudioModeBitResolution longRes = resolveWwiseAudioModeBit(longPkt.data(), longPkt.size(), modeBlockflags);
    CHECK(longRes.ok);
    CHECK(longRes.modeBit == 1);
    CHECK(longRes.isLong);

    // Null/empty packet.
    AudioModeBitResolution emptyRes = resolveWwiseAudioModeBit(nullptr, 0, modeBlockflags);
    CHECK(!emptyRes.ok);

    // Empty modeBlockflags.
    AudioModeBitResolution noModesRes = resolveWwiseAudioModeBit(shortPkt.data(), shortPkt.size(), {});
    CHECK(!noModesRes.ok);

    // Out-of-range mode bit against a 1-entry modeBlockflags list.
    AudioModeBitResolution oorRes = resolveWwiseAudioModeBit(longPkt.data(), longPkt.size(), {false});
    CHECK(!oorRes.ok);
    CHECK(oorRes.diagnostic.find("beyond") != std::string::npos);
}

// ---------------------------------------------------------------------
// Integration-style coverage of the one-packet-submission-lag pattern
// real_vorbis_decode.cpp's own per-packet loop uses (reproduced here
// directly over a small synthetic 4-packet "stream," rather than through a
// full decode harness, to pin the composed resolveWwiseAudioModeBit() +
// rebuildWwiseAudioPacket() behavior independent of libvorbis/container
// plumbing). Mode sequence: long, short, long, long - exercises a long
// block with no previous neighbor (stream start), a short block (no window
// flags at all), a long block with BOTH neighbors known-different
// (short-before/long-after), and a long block with no next neighbor
// (stream end). Also demonstrates, by construction, that this pattern
// never needs a function signature that accepts a neighboring packet's own
// raw bytes: each packet's bytes are touched only once, at its own loop
// iteration.
void testSequentialLagPattern_FourPacketSyntheticStream_AllWindowFlagsMatchHandComputedExpectation() {
    const std::vector<bool> modeBlockflags = {false, true};  // 0=short, 1=long
    std::vector<std::vector<uint8_t>> packets = {
        buildRawWwiseAudioPacket(/*modeBit*/ 1, 0b101, 3),   // packet 0: long (stream start, no prev)
        buildRawWwiseAudioPacket(/*modeBit*/ 0, 0b11, 2),    // packet 1: short
        buildRawWwiseAudioPacket(/*modeBit*/ 1, 0b1001, 4),  // packet 2: long (prev=short, next=long)
        buildRawWwiseAudioPacket(/*modeBit*/ 1, 0b0, 1),     // packet 3: long (stream end, no next)
    };
    // Hand-computed expected (packetTypeBit, modeBit, [prevFlag, nextFlag if long]):
    //   packet 0: type=0 mode=1 prev=0(no prev) next=0(packet1 is short)
    //   packet 1: type=0 mode=0 (short - no window-flag bits at all)
    //   packet 2: type=0 mode=1 prev=0(packet1 short) next=1(packet3 long)
    //   packet 3: type=0 mode=1 prev=1(packet2 long) next=0(no next)

    std::vector<std::vector<uint8_t>> rebuiltPackets(packets.size());

    bool havePending = false;
    size_t pendingIdx = 0;
    bool pendingHasPrev = false, pendingPrevIsLong = false;
    bool haveLastResolved = false, lastResolvedIsLong = false;

    auto finalize = [&](size_t idx, bool hasPrev, bool prevIsLong, bool hasNext, bool nextIsLong) {
        AudioPacketRebuildResult r = rebuildWwiseAudioPacket(packets[idx].data(), packets[idx].size(), hasPrev,
                                                              prevIsLong, hasNext, nextIsLong, modeBlockflags);
        CHECK(r.ok);
        rebuiltPackets[idx] = r.standardPacket;
    };

    for (size_t i = 0; i < packets.size(); ++i) {
        AudioModeBitResolution cur = resolveWwiseAudioModeBit(packets[i].data(), packets[i].size(), modeBlockflags);
        CHECK(cur.ok);
        if (havePending) {
            finalize(pendingIdx, pendingHasPrev, pendingPrevIsLong, /*hasNext=*/true, /*nextIsLong=*/cur.isLong);
        }
        pendingIdx = i;
        pendingHasPrev = haveLastResolved;
        pendingPrevIsLong = lastResolvedIsLong;
        havePending = true;
        haveLastResolved = true;
        lastResolvedIsLong = cur.isLong;
    }
    if (havePending) finalize(pendingIdx, pendingHasPrev, pendingPrevIsLong, /*hasNext=*/false, /*nextIsLong=*/false);

    BitReader br0(rebuiltPackets[0]);
    CHECK(br0.readBits(1) == 0);
    CHECK(br0.readBits(1) == 1);
    CHECK(br0.readBits(1) == 0);  // prev: no previous packet
    CHECK(br0.readBits(1) == 0);  // next: packet 1 is short

    BitReader br1(rebuiltPackets[1]);
    CHECK(br1.readBits(1) == 0);
    CHECK(br1.readBits(1) == 0);  // short - no window-flag bits follow
    CHECK(br1.readBits(2) == 0b11u);

    BitReader br2(rebuiltPackets[2]);
    CHECK(br2.readBits(1) == 0);
    CHECK(br2.readBits(1) == 1);
    CHECK(br2.readBits(1) == 0);  // prev: packet 1 is short
    CHECK(br2.readBits(1) == 1);  // next: packet 3 is long

    BitReader br3(rebuiltPackets[3]);
    CHECK(br3.readBits(1) == 0);
    CHECK(br3.readBits(1) == 1);
    CHECK(br3.readBits(1) == 1);  // prev: packet 2 is long
    CHECK(br3.readBits(1) == 0);  // next: no following packet
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
    run("testFindRiffSubChunk_LocatesVorbAndRejectsMissingOrInvalid",
        testFindRiffSubChunk_LocatesVorbAndRejectsMissingOrInvalid);
    run("testParseVorbChunk_CorrectedBytePositions", testParseVorbChunk_CorrectedBytePositions);
    run("testParseStandardWaveFmt_DecodesChannelsAndRate", testParseStandardWaveFmt_DecodesChannelsAndRate);
    run("testWalkFramedPackets_WellFormedAndTruncated", testWalkFramedPackets_WellFormedAndTruncated);
    run("testAttemptDecode_ContainerLevelRejections", testAttemptDecode_ContainerLevelRejections);
    run("testTrimPcmFramesToSampleCount_TrimsDownNeverPadsOrNoOps",
        testTrimPcmFramesToSampleCount_TrimsDownNeverPadsOrNoOps);
    run("testWavWriter_RoundTripDurationAndHeaderShape", testWavWriter_RoundTripDurationAndHeaderShape);
    run("testRebuildWwiseAudioPacket_ShortBlock_PrefixShapeAndPayloadPassthrough",
        testRebuildWwiseAudioPacket_ShortBlock_PrefixShapeAndPayloadPassthrough);
    run("testRebuildWwiseAudioPacket_LongBlock_WindowFlagsFromNeighborModes",
        testRebuildWwiseAudioPacket_LongBlock_WindowFlagsFromNeighborModes);
    run("testRebuildWwiseAudioPacket_StreamBoundaries_NoNeighborMeansShortWindowFlag",
        testRebuildWwiseAudioPacket_StreamBoundaries_NoNeighborMeansShortWindowFlag);
    run("testRebuildWwiseAudioPacket_IgnoresIsLongWhenHasFlagIsFalse",
        testRebuildWwiseAudioPacket_IgnoresIsLongWhenHasFlagIsFalse);
    run("testRebuildWwiseAudioPacket_RejectsEmptyInputAndOutOfRangeModeBit",
        testRebuildWwiseAudioPacket_RejectsEmptyInputAndOutOfRangeModeBit);
    run("testResolveWwiseAudioModeBit_ShortAndLongAndErrors", testResolveWwiseAudioModeBit_ShortAndLongAndErrors);
    run("testSequentialLagPattern_FourPacketSyntheticStream_AllWindowFlagsMatchHandComputedExpectation",
        testSequentialLagPattern_FourPacketSyntheticStream_AllWindowFlagsMatchHandComputedExpectation);

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_wwise_vorbis_setup_test: all checks passed\n";
    return 0;
}
