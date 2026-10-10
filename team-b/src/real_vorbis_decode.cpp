// See include/sr3audioruntime/real_vorbis_decode.h for the full scope note
// (the one concrete hypothesis this file tests, and what a rejection does
// and does not mean).

#include "sr3audioruntime/real_vorbis_decode.h"

#include <ogg/ogg.h>
#include <vorbis/codec.h>

#include <string>
#include <vector>

#include "sr3audioruntime/wwise_setup_rebuild.h"
#include "sr3audioruntime/wwise_vorbis_setup.h"

namespace sr3audioruntime {
namespace {

// Matches libvorbis's own (non-exported) `_v_writestring` exactly: one byte
// per oggpack_write call, 8 bits each - read directly from this project's
// own vendored third_party/libvorbis/lib/info.c (_vorbis_pack_info/
// _vorbis_pack_comment) to get the bit-for-bit write ORDER right, since
// `oggpack_buffer` packs sub-byte fields LSB-first and this project cannot
// call the real (static, non-exported) pack functions directly.
void writeString(oggpack_buffer& b, const char* s, int len) {
    for (int i = 0; i < len; ++i) {
        oggpack_write(&b, static_cast<unsigned long>(static_cast<unsigned char>(s[i])), 8);
    }
}

std::vector<uint8_t> finishPacket(oggpack_buffer& b) {
    std::vector<uint8_t> out(oggpack_get_buffer(&b), oggpack_get_buffer(&b) + oggpack_bytes(&b));
    oggpack_writeclear(&b);
    return out;
}

// Packet type 1 (identification), built field-for-field in the exact order
// `_vorbis_unpack_info` (info.c) reads them - CONFIRMED by direct read of
// this project's own vendored copy, not guessed.
std::vector<uint8_t> buildIdHeaderPacket(int channels, uint32_t sampleRate, uint8_t bs0exp, uint8_t bs1exp) {
    oggpack_buffer b;
    oggpack_writeinit(&b);
    oggpack_write(&b, 0x01, 8);
    writeString(b, "vorbis", 6);
    oggpack_write(&b, 0, 32);  // vorbis_version, must be 0
    oggpack_write(&b, static_cast<unsigned long>(channels), 8);
    oggpack_write(&b, sampleRate, 32);
    oggpack_write(&b, 0, 32);  // bitrate_upper (unknown/unused by this bring-up)
    oggpack_write(&b, 0, 32);  // bitrate_nominal
    oggpack_write(&b, 0, 32);  // bitrate_lower
    oggpack_write(&b, bs0exp, 4);
    oggpack_write(&b, bs1exp, 4);
    oggpack_write(&b, 1, 1);  // EOP framing bit
    return finishPacket(b);
}

// Packet type 3 (comment), empty vendor string + zero comments - this
// project needs no real comment content for decode (Vorbis's own decode
// math never consults the comment header), only a structurally valid one.
std::vector<uint8_t> buildCommentHeaderPacket() {
    oggpack_buffer b;
    oggpack_writeinit(&b);
    oggpack_write(&b, 0x03, 8);
    writeString(b, "vorbis", 6);
    oggpack_write(&b, 0, 32);  // vendor string length
    oggpack_write(&b, 0, 32);  // comment count
    oggpack_write(&b, 1, 1);   // EOP framing bit
    return finishPacket(b);
}

// Packet type 5 (setup/books) used to be built here the same naive way as
// the id/comment headers above - the standard 7-byte preamble directly
// followed by the REAL Wwise stripped setup-packet bytes, unchanged. That
// hypothesis is now a settled, dated NEGATIVE result (real_vorbis_decode_
// test.cpp's own pinned OV_EBADHEADER-on-every-real-sample regression,
// 2026-10-08): the stripped setup packet is not simply "standard minus
// framing." The real bit-level transcode this requires now lives in
// wwise_setup_rebuild.h/.cpp (spec-audio-runtime.md Sec11.11.3a) - see
// attemptWwiseVorbisDecode() below for how its output is used.

}  // namespace

// See real_vorbis_decode.h's own header comment on this function for the
// full field citation (spec-audio-runtime.md Sec11.11.2/Sec11.11.5) and the
// units/no-op-condition reasoning. Deliberately NOT in the anonymous
// namespace above - exposed so tests/real_vorbis_decode_test.cpp (or a
// synthetic unit test) can exercise the trim logic directly without needing
// a real encoded Vorbis bitstream.
uint64_t trimPcmFramesToSampleCount(std::vector<int16_t>& pcm, int channels, uint64_t currentFrameCount,
                                     uint64_t vorbSampleCountField) {
    if (channels <= 0) return currentFrameCount;
    if (vorbSampleCountField == 0) return currentFrameCount;
    if (vorbSampleCountField >= currentFrameCount) return currentFrameCount;
    pcm.resize(vorbSampleCountField * static_cast<uint64_t>(channels));
    return vorbSampleCountField;
}

namespace {

ogg_packet makePacket(std::vector<uint8_t>& storage, bool bos, int64_t packetno) {
    ogg_packet op{};
    op.packet = storage.data();
    op.bytes = static_cast<long>(storage.size());
    op.b_o_s = bos ? 1 : 0;
    op.e_o_s = 0;
    op.granulepos = 0;
    op.packetno = packetno;
    return op;
}

}  // namespace

DecodeAttemptResult attemptWwiseVorbisDecode(const uint8_t* riffBlob, size_t riffBlobSize) {
    DecodeAttemptResult result;

    VorbisContainerChunks chunks = parseRiffWaveContainer(riffBlob, riffBlobSize);
    if (!chunks.valid || !chunks.hasFmt || !chunks.hasData) {
        result.stageReached = DecodeStage::kContainer;
        result.diagnostic = "RIFF/WAVE container parse failed or missing fmt/data chunk (rejectCode=" +
                             std::to_string(chunks.rejectCode) + ")";
        return result;
    }
    StandardWaveFmt fmt = parseStandardWaveFmt(riffBlob, chunks.fmtOffset, chunks.fmtSize);
    if (!fmt.present || fmt.channels == 0 || fmt.samplesPerSec == 0) {
        result.stageReached = DecodeStage::kContainer;
        result.diagnostic = "fmt chunk present but unparseable (too short, or zero channels/rate)";
        return result;
    }
    auto vorbLoc = findRiffSubChunk(riffBlob, riffBlobSize, kFourCcVorb);
    if (!vorbLoc) {
        result.stageReached = DecodeStage::kContainer;
        result.diagnostic = "no 'vorb' chunk found - not a Wwise-Vorbis-wrapped RIFF blob";
        return result;
    }
    WwiseVorbChunk vorb = parseVorbChunk(riffBlob, riffBlobSize, vorbLoc->first);
    if (!vorb.present) {
        result.stageReached = DecodeStage::kContainer;
        result.diagnostic = "'vorb' chunk found but too short to hold its CONFIRMED 42 bytes + the preceding fallback dword";
        return result;
    }
    if (!vorb.blockSizesWithinVorbisSpecRange()) {
        result.stageReached = DecodeStage::kContainer;
        result.diagnostic = "vorb block-size exponents out of the public Vorbis I spec's legal range "
                             "(64<=bs0<=bs1<=8192): bs0=" +
                             std::to_string(vorb.blockSize0) + " bs1=" + std::to_string(vorb.blockSize1);
        return result;
    }

    // [CONFIRMED] (2026-10-08, Team A's own direct disassembly, commit
    // 8871fb6 on `main`, spec-audio-runtime.md Sec11.11.5's correction -
    // reading `0x00fa4250`/`0x00fa36b0` directly, not decompiler output;
    // cherry-picked onto this branch spec-text-only): the packet stream
    // does NOT begin at the "data" sub-chunk's own first byte - it begins
    // AFTER a leading per-packet seek/index table whose own byte length is
    // `vorb[16:20)`. `this+0x48` (the formula Team B's own prior pass on
    // this branch population-wide-falsified, results/probe_vorb_seektable_
    // relative_offsets_20261008.md) was a correct NUMBER under the WRONG
    // NAME - it is the first-audio-packet offset, not an input to the
    // setup-packet offset. The real setup-packet offset is
    // `dataChunkStart + vorb[16:20)` ALONE. Independently re-derived by
    // this project's own real-data bit-trace BEFORE Team A's correction
    // landed (tools/wwise_vorb_setup_audio_dword_probe.cpp,
    // tools/wwise_setup_packet_bit_trace_probe.cpp: INVARIANT_EXACT 493/493
    // - the setup packet's own declared 2-byte-length-prefixed payload
    // lands EXACTLY on `dataChunkStart + vorb[20:24)` for every real record
    // - and cross-checked against Team A's own stated identity,
    // `vorb[20:24) == vorb[4:8) + vorb[16:20)`, 493/493 on this project's
    // own corpus) and against vgmstream's own public-source packet-header
    // handling (`src/coding/vorbis_custom_utils_wwise.c`, read via WebFetch
    // for format understanding only, not transcribed: `WWVORBIS_V53` -
    // this project's own confirmed vorb-chunk-size-0x2a version - uses
    // `WWV_TYPE_2`, a bare 2-byte size prefix with no granule field,
    // confirming the EXISTING packet-header-shape assumption below was
    // never the problem, only the starting offset was).
    //
    // Since the setup packet's own declared length provably reaches
    // `dataChunkStart + vorb[20:24)` EXACTLY (the INVARIANT_EXACT finding
    // above), a single continuous walkFramedPackets() call starting at
    // `setupPacketStart` naturally produces the setup packet as packets[0]
    // and every real audio packet as packets[1..], all the way to the end
    // of "data" - no second starting point is needed.
    const uint32_t seekTableLenField =
        static_cast<uint32_t>(vorb.raw42[16]) | (static_cast<uint32_t>(vorb.raw42[17]) << 8) |
        (static_cast<uint32_t>(vorb.raw42[18]) << 16) | (static_cast<uint32_t>(vorb.raw42[19]) << 24);
    const size_t setupPacketStart = chunks.dataOffset + seekTableLenField;
    if (setupPacketStart < chunks.dataOffset || setupPacketStart >= chunks.dataOffset + chunks.dataSize) {
        result.stageReached = DecodeStage::kContainer;
        result.diagnostic = "setupPacketStart (dataChunkStart + vorb[16:20)) falls outside the 'data' sub-chunk";
        return result;
    }
    const size_t walkSize = (chunks.dataOffset + chunks.dataSize) - setupPacketStart;
    FramedPacketWalkResult framed = walkFramedPackets(riffBlob, setupPacketStart, walkSize);
    if (framed.packets.empty()) {
        result.stageReached = DecodeStage::kContainer;
        result.diagnostic = "packet-framing walk (Sec11.11.1, from the [CONFIRMED] setupPacketStart) produced zero packets";
        return result;
    }
    // framed.packets[0] = the stripped Vorbis setup packet (Sec11.11.3);
    // framed.packets[1..] = compressed audio packets, in order.

    vorbis_info vi;
    vorbis_info_init(&vi);
    vorbis_comment vc;
    vorbis_comment_init(&vc);

    std::vector<uint8_t> idPkt =
        buildIdHeaderPacket(fmt.channels, fmt.samplesPerSec, vorb.blockSizeExponent0, vorb.blockSizeExponent1);
    ogg_packet opId = makePacket(idPkt, true, 0);
    int rc = vorbis_synthesis_headerin(&vi, &vc, &opId);
    if (rc != 0) {
        result.stageReached = DecodeStage::kIdHeader;
        result.libvorbisErrorCode = rc;
        result.diagnostic =
            "vorbis_synthesis_headerin rejected the RECONSTRUCTED identification header (built from this "
            "project's own CONFIRMED fmt-chunk channel/rate + vorb-chunk block-size-exponent fields) - a "
            "failure here is a bug in THIS code, not evidence about Wwise's own format; libvorbis error=" +
            std::to_string(rc);
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        return result;
    }
    // Captured HERE, right after the id header's own real acceptance - not
    // after the setup-header call below. libvorbis's own `_vorbis_unpack_
    // books` (third_party/libvorbis/lib/info.c, read directly) calls
    // `vorbis_info_clear(vi)` on ANY setup-header failure, which zeroes
    // vi.channels/vi.rate - capturing after that call would wrongly report
    // channels=0/sampleRate=0 even on a file where the id header (this
    // project's own construction) was genuinely, independently accepted.
    result.channels = vi.channels;
    result.sampleRate = vi.rate;

    std::vector<uint8_t> commentPkt = buildCommentHeaderPacket();
    ogg_packet opComment = makePacket(commentPkt, false, 1);
    rc = vorbis_synthesis_headerin(&vi, &vc, &opComment);
    if (rc != 0) {
        result.stageReached = DecodeStage::kCommentHeader;
        result.libvorbisErrorCode = rc;
        result.diagnostic = "vorbis_synthesis_headerin rejected the RECONSTRUCTED (empty) comment header - "
                             "this project's own construction; libvorbis error=" +
                             std::to_string(rc);
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        return result;
    }

    // THE real transcode (wwise_setup_rebuild.h, spec-audio-runtime.md
    // Sec11.11.3a): rebuilds a standard-conformant Vorbis I setup header
    // from the real Wwise stripped setup packet's own bytes, rather than
    // forwarding them unchanged (the superseded, now-dated-negative
    // hypothesis this file used to test - see this file's own comment
    // above buildIdHeaderPacket/buildCommentHeaderPacket's sibling that
    // used to live here).
    SetupRebuildResult rebuilt = rebuildVorbisSetupHeader(
        riffBlob + framed.packets[0].offset, framed.packets[0].length, fmt.channels);
    if (!rebuilt.ok) {
        result.stageReached = DecodeStage::kSetupHeader;
        result.libvorbisErrorCode = 0;  // this project's own transcode failed before ever calling libvorbis
        result.diagnostic = "wwise_setup_rebuild's own bitstream transcode failed (never reached libvorbis): " +
                             rebuilt.diagnostic;
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        return result;
    }

    std::vector<uint8_t> setupPkt = rebuilt.standardSetupPacket;
    ogg_packet opSetup = makePacket(setupPkt, false, 2);
    rc = vorbis_synthesis_headerin(&vi, &vc, &opSetup);
    // result.channels/result.sampleRate were already captured right after
    // the id header's own acceptance, above - NOT re-read here, since a
    // setup-header failure clears vi (see that capture site's own comment).
    if (rc != 0) {
        result.stageReached = DecodeStage::kSetupHeader;
        result.libvorbisErrorCode = rc;
        result.diagnostic =
            "libvorbis rejected this project's OWN REBUILT standard setup header (wwise_setup_rebuild.h, " +
            rebuilt.diagnostic + ") - libvorbis error=" + std::to_string(rc) +
            ". Either the rebuild's own still-OPEN inferences (see that file's own header comment) are wrong for "
            "this specific record, or this record exercises a real-bitstream shape this project's transcode does "
            "not yet handle.";
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        return result;
    }

    // All three header packets accepted by the real, independent libvorbis
    // parser - a genuinely confirmed empirical result, not assumed.
    vorbis_dsp_state vd;
    if (vorbis_synthesis_init(&vd, &vi) != 0) {
        result.stageReached = DecodeStage::kSetupHeader;
        result.diagnostic = "all 3 header packets accepted, but vorbis_synthesis_init() itself failed "
                             "(rebuilt.codebookCount=" + std::to_string(rebuilt.codebookCount) + ")";
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        return result;
    }
    vorbis_block vb;
    vorbis_block_init(&vd, &vb);

    std::vector<int16_t> pcmOut;
    uint64_t attempted = 0, decodedOk = 0;
    bool failed = false;
    std::string failureDiagnostic;
    int64_t failedIndex = -1;

    // Rebuilds, submits to `vorbis_synthesis()`, and (on success) drains
    // the PCM for exactly one packet (`idx`), given its own already-known
    // neighbor flags. Shared by both the main loop's one-packet-lag
    // finalize step and the post-loop flush of the last pending packet,
    // below.
    auto emitPacket = [&](const uint8_t* ptr, size_t len, size_t idx, bool hasPrev, bool prevIsLong, bool hasNext,
                          bool nextIsLong) {
        AudioPacketRebuildResult rebuiltAudio =
            rebuildWwiseAudioPacket(ptr, len, hasPrev, prevIsLong, hasNext, nextIsLong, rebuilt.modeBlockflags);
        ++attempted;
        if (!rebuiltAudio.ok) {
            failed = true;
            failedIndex = static_cast<int64_t>(idx);
            failureDiagnostic = "rebuildWwiseAudioPacket() itself failed (never reached vorbis_synthesis()): " +
                                 rebuiltAudio.diagnostic;
            return;
        }

        ogg_packet opAudio = makePacket(rebuiltAudio.standardPacket, false, static_cast<int64_t>(2 + idx));
        const int synthRc = vorbis_synthesis(&vb, &opAudio);
        if (synthRc != 0) {
            // UPDATE 2026-10-09 (Wwise modified-packet rebuild task): this
            // project's own "no invented fixes" / anti-splice-survivors
            // instruction - ANY single failed audio packet fails the WHOLE
            // decode now. The prior behavior (silently `continue`-ing past
            // a failure and splicing the survivors together, gated only on
            // `decodedOk == 0`) is exactly what produced 5 "successful"
            // decodes that an independent oracle (vgmstream-cli) found were
            // pure noise - see results/verify_vorbis_oracle_comparison_
            // 20261008.md. Splicing non-contiguous Vorbis audio packets
            // breaks the lapped-MDCT overlap-add every subsequent packet's
            // own decode depends on, so a single real failure anywhere in
            // the stream means NOTHING after it can be trusted either.
            failed = true;
            failedIndex = static_cast<int64_t>(idx);
            failureDiagnostic = "vorbis_synthesis() rejected the rebuilt packet with libvorbis error=" +
                                 std::to_string(synthRc);
            return;
        }
        vorbis_synthesis_blockin(&vd, &vb);
        ++decodedOk;
        float** pcm = nullptr;
        int samples;
        while ((samples = vorbis_synthesis_pcmout(&vd, &pcm)) > 0) {
            for (int s = 0; s < samples; ++s) {
                for (int c = 0; c < vi.channels; ++c) {
                    float v = pcm[c][s];
                    if (v > 1.0f) v = 1.0f;
                    if (v < -1.0f) v = -1.0f;
                    pcmOut.push_back(static_cast<int16_t>(v * 32767.0f));
                }
            }
            vorbis_synthesis_read(&vd, samples);
        }
    };

    // Sequential, state-carried rebuild per spec-audio-runtime.md
    // Sec11.11.4a, and per this task's own explicit correction of the
    // prior (uncommitted) attempt's own neighbor-byte-peek implementation
    // (see wwise_setup_rebuild.h's own header comment on
    // rebuildWwiseAudioPacket()/resolveWwiseAudioModeBit() for the full
    // story of what was wrong with that first attempt and why): a packet's
    // long-block window flags depend on the IMMEDIATELY adjacent packets'
    // own mode bits, which this exe derives from running decode state
    // carried forward one packet at a time - never by reading another
    // packet's raw bytes out of its own turn. Reproduced here as an
    // explicit one-packet SUBMISSION LAG: packet i's own mode bit is read
    // exactly once, at the loop iteration where i is "current" below (its
    // own turn, nowhere else); packet i itself is only rebuilt and handed
    // to `vorbis_synthesis()` (via emitPacket(), above) once the NEXT
    // iteration has read packet i+1's own mode bit (supplying packet i's
    // own nextWindowFlag) - or, for the stream's last packet, once the loop
    // ends and no next packet exists. Packet i's own previousWindowFlag is
    // already known two iterations earlier (it is packet i-1's own mode
    // bit, read at THAT iteration's turn) - no lookahead is ever needed for
    // it. framed.packets[0] is the setup packet, never a neighbor here;
    // audio packets start at index 1.
    bool havePending = false;
    size_t pendingIndex = 0;
    const uint8_t* pendingPtr = nullptr;
    size_t pendingLen = 0;
    bool pendingHasPrev = false;
    bool pendingPrevIsLong = false;
    bool haveLastResolved = false;  // becomes the NEXT pending packet's own "previous" state
    bool lastResolvedIsLong = false;

    for (size_t i = 1; i < framed.packets.size(); ++i) {
        const uint8_t* curPtr = riffBlob + framed.packets[i].offset;
        const size_t curLen = framed.packets[i].length;

        // Packet i's OWN single mode-selector bit, read at packet i's OWN
        // turn - this is the only bit ever read from packet i before it
        // becomes "pending" below. Never a neighbor's bytes, never read
        // early.
        AudioModeBitResolution curMode = resolveWwiseAudioModeBit(curPtr, curLen, rebuilt.modeBlockflags);
        if (!curMode.ok) {
            // Honest edge case, not guessed around: if packet i's own mode
            // bit can't even be resolved, the PENDING packet (i-1, already
            // fully valid at ITS own mode-bit resolution) can never learn
            // its own correct nextWindowFlag either - both are reported as
            // one failure, at index i (the proximate cause), and the
            // pending packet is deliberately NOT separately finalized with
            // a guessed flag.
            failed = true;
            failedIndex = static_cast<int64_t>(i);
            failureDiagnostic = "resolveWwiseAudioModeBit() failed on packet " + std::to_string(i) + " (blocks " +
                                 (havePending ? "finalizing pending packet " + std::to_string(pendingIndex) +
                                                    " as well, since its own nextWindowFlag needs this packet's "
                                                    "mode bit"
                                              : "nothing else pending") +
                                 "): " + curMode.diagnostic;
            break;
        }

        if (havePending) {
            // Packet i's own mode bit, just read, IS packet (i-1)'s own
            // nextWindowFlag source - known now purely because the loop's
            // own forward progress naturally reached packet i, not because
            // anything peeked ahead out of turn.
            emitPacket(pendingPtr, pendingLen, pendingIndex, pendingHasPrev, pendingPrevIsLong, /*hasNext=*/true,
                       /*nextIsLong=*/curMode.isLong);
            if (failed) break;
        }

        pendingIndex = i;
        pendingPtr = curPtr;
        pendingLen = curLen;
        pendingHasPrev = haveLastResolved;
        pendingPrevIsLong = lastResolvedIsLong;
        havePending = true;

        haveLastResolved = true;
        lastResolvedIsLong = curMode.isLong;
    }

    if (!failed && havePending) {
        // The stream's last packet - no next packet exists, per
        // rebuildWwiseAudioPacket()'s own documented end-of-stream
        // convention (nextWindowFlag=0).
        emitPacket(pendingPtr, pendingLen, pendingIndex, pendingHasPrev, pendingPrevIsLong, /*hasNext=*/false,
                   /*nextIsLong=*/false);
    }

    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);

    result.audioPacketsTotal = attempted;
    result.audioPacketsDecodedOk = decodedOk;
    result.firstFailedAudioPacketIndex = failedIndex;
    result.stageReached = DecodeStage::kAudioPackets;
    if (failed) {
        result.diagnostic = "audio packet index " + std::to_string(failedIndex) + " (of " +
                             std::to_string(framed.packets.size() - 1) + " real audio packets) failed: " +
                             failureDiagnostic + " - " + std::to_string(decodedOk) +
                             " packet(s) before it decoded successfully, but the WHOLE decode is reported as "
                             "failed (no splice-survivors: a single failed packet breaks lapped-MDCT "
                             "continuity for everything after it)";
        return result;
    }
    if (decodedOk == 0) {
        // Only reachable if framed.packets.size() == 1 (a setup packet with
        // zero real audio packets at all) - `attempted` would also be 0
        // here, a genuinely different case from "every packet failed"
        // (which is now folded into the `failed` branch above).
        result.diagnostic = "all 3 header packets accepted (a genuinely real result), but the stream has zero "
                             "real audio packets to decode";
        return result;
    }

    result.pcm = std::move(pcmOut);
    result.totalSampleFrames = result.channels > 0 ? result.pcm.size() / static_cast<uint64_t>(result.channels) : 0;

    // Sec11.11.2/Sec11.11.5 sample-count trim (2026-10-09 task - closes the
    // "~0.01-0.02s longer than the oracle" residual results/verify_vorbis_
    // oracle_comparison_20261009_post_packet_rebuild_fix.md flagged: "the
    // oracle's own full sample sequence is an exact prefix of this project's
    // own output... consistent with this project's own decode including the
    // lapped-MDCT's final block's natural 'ring-out'/padding samples that a
    // real... exact-sample-count field this project does not yet consult
    // would normally discard"). `vorb.sampleCountFallbackField` IS that
    // field (raw42[0..3], see real_vorbis_decode.h's own header comment on
    // trimPcmFramesToSampleCount() for the full citation) - trim down to AT
    // MOST that many frames, never pad.
    const uint64_t preTrimFrames = result.totalSampleFrames;
    result.totalSampleFrames = trimPcmFramesToSampleCount(result.pcm, result.channels, result.totalSampleFrames,
                                                            vorb.sampleCountFallbackField);

    result.stageReached = DecodeStage::kOk;
    result.ok = true;
    result.diagnostic = "REAL DECODE SUCCEEDED: " + std::to_string(decodedOk) + "/" + std::to_string(attempted) +
                         " audio packets decoded (EVERY attempted packet succeeded, no splice-survivors), " +
                         std::to_string(result.totalSampleFrames) + " sample frames (pre-trim " +
                         std::to_string(preTrimFrames) + ", vorb.sampleCountFallbackField=" +
                         std::to_string(vorb.sampleCountFallbackField) + ")";
    return result;
}

}  // namespace sr3audioruntime
