// Real-data regression for the audio bring-up task's Wwise-Vorbis decode
// attempt (include/sr3audioruntime/{wwise_vorbis_setup.h,real_vorbis_decode.h,
// wwise_setup_rebuild.h}, spec-audio-runtime.md Sec11.11, unit U-W4c). Reads
// every real `_media.bnk_pc` entry out of `cutscene_sounds.vpp_pc` (the
// smallest of the four raw/uncompressed audio archives, spec-audio-format.md
// Sec2) from CRREISH_GAME_CACHE_DIR and exits 77 - reported by ctest as
// SKIPPED, never as a pass - when it is not there (CI, other machines),
// matching real_zone_objects_test.cpp's own convention exactly.
//
// Nothing from the archive is copied into the repository.
//
// WHAT THIS TEST PINS, AND WHY (this project's own direct measurement,
// 2026-10-08):
//
//   1. Every real "vorb" chunk's block-size exponent pair decodes to
//      EXACTLY 256/2048, and every record's RECONSTRUCTED Vorbis
//      identification/comment headers (built from the real fmt-chunk
//      channels/rate + the vorb-chunk block-size exponents) are ACCEPTED by
//      the real, independent libvorbis parser - proof the container-level
//      reconstruction itself is correct, not just internally self-
//      consistent. Unchanged from this test's own earlier pass.
//
//   2. THE SETUP-PACKET TRANSCODE (wwise_setup_rebuild.h, spec-audio-
//      runtime.md Sec11.11.3a): this project's own bit-level transcoder
//      from the real Wwise stripped setup packet to a standard-conformant
//      Vorbis I setup header. This SUPERSEDES the earlier "forward the raw
//      stripped bytes unchanged" hypothesis, which this test used to pin as
//      a uniform OV_EBADHEADER (-133) result on every sample - that
//      hypothesis is now a settled, dated NEGATIVE result (see
//      wwise_setup_rebuild.h's own header comment for the full empirical
//      story: the stripped format really does differ bit-for-bit from
//      standard Vorbis I, not just in framing).
//
//      HONEST CURRENT STATE OF THE TRANSCODE, UPDATED 2026-10-08 (dense-
//      codebook-grammar fix task) - the paragraph this replaces described
//      an even earlier, now-doubly-superseded bring-up stage ("dim==0
//      trivial slot," "maptype is 2 bits") that predates Sec11.11.3a's own
//      MAJOR CORRECTION (the 10-bit-index-into-a-599-entry-static-library
//      mechanism) entirely; see wwise_setup_rebuild.h's own header comment
//      for that history. As of THIS pass: the dense/unordered codebook
//      body grammar (the one remaining blocker Sec11.11.3a's MAJOR
//      CORRECTION left open) is now RESOLVED and bit-exact verified
//      against all 599 real library codebooks (zero parse failures, zero
//      field mismatches - see tests/wwise_codebook_library_crosscheck_test.cpp).
//      On the real 493-record population below, this unlocks real
//      progress - 5/493 records now reach libvorbis's own
//      vorbis_synthesis_headerin() for the setup header (rejected there
//      with exactly OV_EBADHEADER; previously 0/493 ever got that far) -
//      but 488/493 still fail before reaching libvorbis, now at a
//      different point: not inside the codebook-body grammar (proven
//      airtight), but in the MAIN packet's own per-codebook 10-bit index
//      list occasionally resolving to a value spec-audio-codebook-
//      library.md's own Provenance section calls spurious (>= 599). This
//      is reported as a genuinely OPEN, not-invented residual - see
//      wwise_setup_rebuild.h's own header comment for the full, dated
//      story and what was hand bit-traced against real data to rule out a
//      Team-B bitstream bug at that specific point.
//
//      This test therefore pins, precisely, TODAY's real measured outcome
//      (not the old "always OV_EBADHEADER" result, not a guessed future
//      state): every record's setup-packet transcode either (a) fails
//      inside this project's own transcoder before ever reaching libvorbis
//      (categorized by which stage/reason below), or (b) reaches libvorbis
//      and is rejected with exactly OV_EBADHEADER, or (c) succeeds. Buckets
//      (a) and (b) are both "not yet decoding" outcomes; only an
//      UNEXPECTED libvorbis error code (anything other than OV_EBADHEADER)
//      is flagged as a real regression needing investigation, same
//      discipline as before.
//
//   3. If real decoded audio IS produced (ok==true for some record), this
//      test freezes an audio golden: the content hash of the raw decoded
//      PCM sample data (not the WAV file's own bytes, since header fields
//      like a timestamp would make file-level hashing fragile), the
//      decoded duration, and a non-silence check (peak absolute sample
//      amplitude above a small noise floor) - written to stdout and to a
//      `.wav` file next to the test binary, per this task's own "freeze it
//      as a new audio golden" requirement. Per this project's own "no
//      invented fixes / skip-and-revisit" policy: document the best-known
//      real state, do not force a result - today's real state is zero
//      successes, reported honestly above, not papered over.
//
//      UPDATE 2026-10-09 (independent-oracle verification task,
//      tests/golden/wwise_vorbis_decode/STATUS.txt): `ok==true` here does
//      NOT mean verified-correct audio - do not treat a nonzero
//      realDecodeSucceeded count as a real success without re-reading that
//      STATUS.txt first. An independent-oracle comparison (vgmstream-cli,
//      run as a black box against the same raw record bytes) found that
//      every one of the 5 records this project's own gate called ok==true
//      shows essentially zero correlation with the real audio at every
//      lag - `attemptWwiseVorbisDecode()`'s own success gate
//      (`decodedOk == 0` check, real_vorbis_decode.cpp) only requires ONE
//      audio packet to survive `vorbis_synthesis()`, silently drops every
//      packet that fails it with no gap-fill, and splices the survivors
//      together - which breaks the lapped-MDCT continuity even a correctly
//      decoded surviving packet depends on. Per-record real
//      `vorbis_synthesis()` success rates measured 1.7%-49.4% of attempted
//      audio packets, not 100%. Full detail:
//      results/verify_vorbis_oracle_comparison_20261008.md. Not fixed here
//      (no invented fixes without further investigation) - this test's own
//      CHECKs below are unchanged and still pass on today's real data; this
//      note exists so a future reader does not mistake `ok==true` for a
//      verified decode.
//
//      UPDATE 2026-10-09 (Wwise modified-packet rebuild task): the root
//      cause the UPDATE above left open is now fixed. spec-audio-
//      runtime.md Sec11.11.4a (CONFIRMED from direct disassembly of this
//      exe's own per-packet audio dispatcher, `0x00fa4810`) establishes the
//      real Wwise audio-packet bit layout this project's own code never
//      accounted for: no packet-type bit at all, a single HARDCODED
//      mode-selector bit (not a computed `ceil(log2(modeCount))` field -
//      this project's own original, public-ww2ogg-derived hypothesis for
//      that one field, now a dated negative result), and long-block
//      previous/next window-flag bits that are never present on the wire in
//      any form. `rebuildWwiseAudioPacket()` (wwise_setup_rebuild.h) now
//      reconstructs the standard packet shape libvorbis's own
//      `vorbis_synthesis()` requires before every audio-packet call in
//      real_vorbis_decode.cpp, which also now drops the splice-survivors
//      behavior the UPDATE above flagged: ANY single failed packet fails the
//      WHOLE decode (`ok==true` can no longer mean "some packets survived" -
//      see DecodeAttemptResult::firstFailedAudioPacketIndex). Re-ran the
//      SAME independent oracle comparison against the SAME 5 records -
//      see results/verify_vorbis_oracle_comparison_20261008.md for the
//      numbers this fix achieves (or does not - reported honestly either
//      way, per "no invented fixes"). This test file's own CHECKs below gain
//      one new, real, categorized outcome bucket
//      (`audioPacketFailed`/`audioPacketFailureReasons`) for "setup header
//      accepted, but the strict new gate rejected the record" - a case that
//      could not previously occur (the old lenient gate accepted any
//      nonzero decodedOk), so it needed a real bucket rather than silently
//      falling into the "UNEXPECTED stage" catch-all.

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "sr3audio/media_bank.h"
#include "sr3audioruntime/real_vorbis_decode.h"
#include "sr3audioruntime/wav_writer.h"
#include "sr3audioruntime/wwise_codebook_library.h"
#include "sr3audioruntime/wwise_vorbis_setup.h"
#include "sr3runtimedata/runtime_data_loader.h"
#include "vpp/container.h"

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    if (n < 0) n = 0;
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}

bool fileExists(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return f.good();
}

bool endsWith(const std::string& n, const std::string& s) {
    return n.size() >= s.size() && n.compare(n.size() - s.size(), s.size(), s) == 0;
}

constexpr int kOvEBadHeader = -133;

// FNV-1a over the raw interleaved PCM sample data - the audio golden's
// content hash (per this task's own "hash the raw sample data, not the WAV
// file bytes" requirement, since WAV header fields like a timestamp would
// make file-level hashing fragile).
uint64_t fnv1a64(const std::vector<int16_t>& pcm) {
    uint64_t h = 1469598103934665603ull;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(pcm.data());
    const size_t n = pcm.size() * sizeof(int16_t);
    for (size_t i = 0; i < n; ++i) {
        h ^= bytes[i];
        h *= 1099511628211ull;
    }
    return h;
}

int16_t peakAbsAmplitude(const std::vector<int16_t>& pcm) {
    int16_t peak = 0;
    for (int16_t s : pcm) {
        int16_t a = s < 0 ? static_cast<int16_t>(-(s + 1)) : s;  // avoid INT16_MIN negation UB
        if (a > peak) peak = a;
    }
    return peak;
}

}  // namespace

int main() {
    // 2026-10-10 (runtime data loader task): load the Wwise codebook blob
    // from the player's own game install, independently SHA-256-verified,
    // if it isn't already compiled in - see
    // include/sr3runtimedata/runtime_data_loader.h. A cheap no-op in the
    // ordinary dev build (src/wwise_codebook_library_data.cpp already
    // compiled in). This is the key regression this task asks for: the
    // decode below must be byte-identical whether the codebook data comes
    // from that old compiled-in source or from this new loader.
    sr3runtimedata::ensureRuntimeDataLoaded();
    if (!sr3audioruntime::isWwiseCodebookLibraryDataAvailable()) {
        std::cout << "wwise codebook library data not available (no compiled-in data, and the "
                      "runtime loader found no verified data at the configured game install "
                      "path) - SKIPPED\n";
        return 77;
    }

#ifndef CRREISH_GAME_CACHE_DIR
    std::cout << "CRREISH_GAME_CACHE_DIR not configured - SKIPPED\n";
    return 77;
#else
    const std::string cacheDir = CRREISH_GAME_CACHE_DIR;
    const std::string archivePath = cacheDir + "/cutscene_sounds.vpp_pc";
    if (!fileExists(archivePath)) {
        std::cout << "real-data archive not found (" << archivePath << ") - SKIPPED\n";
        return 77;
    }

    std::vector<uint8_t> archiveBytes = readFile(archivePath);
    if (archiveBytes.empty()) {
        std::cout << "real-data archive empty/unreadable - SKIPPED\n";
        return 77;
    }

    vpp::Container c(vpp::ByteView(archiveBytes.data(), archiveBytes.size()));

    long long banksWalked = 0;
    long long recordsWithVorb = 0;
    long long reconstructedHeaderAccepted = 0;  // got past kIdHeader AND kCommentHeader
    long long setupRejectedExactlyBadHeader = 0;
    long long setupRejectedOtherCode = 0;  // a DIFFERENT, unexpected libvorbis error code - a real regression
    long long transcodeFailedBeforeLibvorbis = 0;  // this project's own rebuild step failed first
    long long audioPacketFailed = 0;  // setup header accepted, but an audio packet failed (see UPDATE below)
    long long realDecodeSucceeded = 0;
    bool wroteFirstWav = false;

    // Categorized failure reasons for transcodeFailedBeforeLibvorbis, per
    // this task's own "failure reasons categorized" requirement - keyed by
    // wwise_setup_rebuild's own diagnostic text with the record-specific
    // "N/M" codebook-index counters stripped out, so repeats collapse into
    // one bucket per underlying cause.
    std::map<std::string, long long> transcodeFailureReasons;
    std::map<std::string, long long> audioPacketFailureReasons;

    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        if (!endsWith(n, "_media.bnk_pc")) continue;

        std::vector<uint8_t> storage;
        vpp::ByteView bytes;
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            bytes = c.rawEntryBytes(i);
        } else {
            auto r = c.decompressEntry(i);
            if (r.data.empty()) continue;
            storage = std::move(r.data);
            bytes = vpp::ByteView(storage.data(), storage.size());
        }

        sr3audio::MediaBank mb;
        try {
            mb = sr3audio::MediaBank::parse(bytes);
        } catch (const std::exception&) {
            continue;
        }
        ++banksWalked;
        const size_t declaredSize = c.entries()[i].payload.decompressedLength;
        std::vector<sr3audio::MediaBankRecord> records;
        try {
            records = mb.walk(declaredSize);
        } catch (const std::exception&) {
            continue;
        }

        for (const auto& rec : records) {
            if (rec.size < 128) continue;
            const uint8_t* recBytes = bytes.data() + rec.offset;

            // Only test records that actually carry a "vorb" chunk (the
            // real shape this entire pipeline targets) - a record without
            // one is simply out of scope for this test, not a failure.
            auto vorbLoc = sr3audioruntime::findRiffSubChunk(recBytes, rec.size, sr3audioruntime::kFourCcVorb);
            if (!vorbLoc) continue;
            sr3audioruntime::WwiseVorbChunk vorb =
                sr3audioruntime::parseVorbChunk(recBytes, rec.size, vorbLoc->first);
            if (!vorb.present) continue;
            ++recordsWithVorb;

            // Pin #1: the corrected block-size-exponent byte position.
            CHECK(vorb.blockSizeExponent0 == 0x08);
            CHECK(vorb.blockSizeExponent1 == 0x0b);
            CHECK(vorb.blockSize0 == 256);
            CHECK(vorb.blockSize1 == 2048);
            CHECK(vorb.blockSizesWithinVorbisSpecRange());

            sr3audioruntime::DecodeAttemptResult res = sr3audioruntime::attemptWwiseVorbisDecode(recBytes, rec.size);

            // Pin #2: the reconstructed id+comment headers are always
            // accepted by the real libvorbis parser - this project's own
            // construction is never the thing that fails.
            CHECK(res.stageReached != sr3audioruntime::DecodeStage::kContainer);
            CHECK(res.stageReached != sr3audioruntime::DecodeStage::kIdHeader);
            CHECK(res.stageReached != sr3audioruntime::DecodeStage::kCommentHeader);
            if (res.stageReached == sr3audioruntime::DecodeStage::kSetupHeader ||
                res.stageReached == sr3audioruntime::DecodeStage::kAudioPackets || res.ok) {
                ++reconstructedHeaderAccepted;
                CHECK(res.channels == 1 || res.channels == 2 || res.channels == 6);
                CHECK(res.sampleRate == 44100 || res.sampleRate == 48000);
            }

            if (res.ok) {
                ++realDecodeSucceeded;
                // Audio golden: content hash of the raw PCM samples (not
                // the WAV file's own bytes), decoded duration, and a
                // non-silence check - this task's own golden-freezing
                // requirement.
                const uint64_t pcmHash = fnv1a64(res.pcm);
                const double durationSeconds =
                    res.sampleRate > 0 ? static_cast<double>(res.totalSampleFrames) / static_cast<double>(res.sampleRate)
                                       : 0.0;
                const int16_t peak = peakAbsAmplitude(res.pcm);
                constexpr int16_t kNoiseFloor = 64;  // ~-48dBFS; silence/near-silence sits well below this
                CHECK(peak > kNoiseFloor);
                std::cout << "*** REAL DECODE (" << n << "): pcmHash=0x" << std::hex << pcmHash << std::dec
                          << " frames=" << res.totalSampleFrames << " channels=" << res.channels
                          << " sampleRate=" << res.sampleRate << " durationSeconds=" << durationSeconds
                          << " peakAbsAmplitude=" << peak << " ***\n";
                if (!wroteFirstWav) {
                    wroteFirstWav = sr3audioruntime::writeWavPcm16("real_vorbis_decode_first_success.wav", res.pcm,
                                                                    static_cast<uint32_t>(res.sampleRate),
                                                                    static_cast<uint16_t>(res.channels));
                    std::cout << "*** FIRST REAL AUDIBLE DECODE SUCCEEDED (" << n
                              << ") - wrote real_vorbis_decode_first_success.wav = " << wroteFirstWav << " ***\n";
                }
            } else if (res.stageReached == sr3audioruntime::DecodeStage::kSetupHeader) {
                if (res.libvorbisErrorCode == 0) {
                    // This project's own wwise_setup_rebuild transcode
                    // failed before ever handing anything to libvorbis -
                    // categorize by WHY (see this file's own header
                    // comment: today's honest, dated state - not all
                    // real setup packets transcode cleanly yet).
                    ++transcodeFailedBeforeLibvorbis;
                    std::string reason = res.diagnostic;
                    // Strip the record-specific "codebook N/M: " counter so
                    // repeats collapse into one bucket per underlying cause.
                    auto colon = reason.find("): ");
                    if (colon != std::string::npos) reason = reason.substr(colon + 3);
                    auto slash = reason.find('/');
                    if (slash != std::string::npos) {
                        auto spaceBefore = reason.rfind(' ', slash);
                        auto colonAfter = reason.find(':', slash);
                        if (spaceBefore != std::string::npos && colonAfter != std::string::npos) {
                            reason = reason.substr(0, spaceBefore + 1) + reason.substr(colonAfter + 2);
                        }
                    }
                    ++transcodeFailureReasons[reason];
                } else if (res.libvorbisErrorCode == kOvEBadHeader) {
                    // Pin #3: libvorbis itself rejected this project's OWN
                    // REBUILT standard header - a real, deterministic
                    // outcome for today's still-incomplete transcode, not
                    // a crash and not a different/unexpected error code.
                    ++setupRejectedExactlyBadHeader;
                } else {
                    ++setupRejectedOtherCode;
                    std::cerr << "UNEXPECTED libvorbis error code at setup header: " << res.libvorbisErrorCode
                              << " (" << n << "): " << res.diagnostic << "\n";
                }
            } else if (res.stageReached == sr3audioruntime::DecodeStage::kAudioPackets) {
                // UPDATE 2026-10-09 (Wwise modified-packet rebuild task): a
                // genuinely NEW, EXPECTED (not "unexpected") outcome bucket,
                // only reachable now that the setup header is accepted AND
                // the audio-packet loop's own strict all-or-nothing gate
                // (real_vorbis_decode.cpp - "drop the splice-survivors
                // behavior") rejects the whole decode on the FIRST packet
                // that fails rebuildWwiseAudioPacket()/vorbis_synthesis().
                // Categorized (not just counted) the same way the setup-
                // header failure reasons above are, so a real regression in
                // the packet-rebuild logic is visible, not silently folded
                // into "unexpected."
                ++audioPacketFailed;
                std::string reason = res.diagnostic;
                auto colon = reason.find("failed: ");
                if (colon != std::string::npos) reason = reason.substr(colon + 8);
                auto dash = reason.find(" - ");
                if (dash != std::string::npos) reason = reason.substr(0, dash);
                ++audioPacketFailureReasons[reason];
                std::cout << "  audio-packet decode failed (" << n
                          << "): firstFailedAudioPacketIndex=" << res.firstFailedAudioPacketIndex
                          << " audioPacketsDecodedOk=" << res.audioPacketsDecodedOk
                          << " audioPacketsTotal=" << res.audioPacketsTotal << " reason=" << reason << "\n";
            } else {
                std::cerr << "UNEXPECTED decode stage/outcome (" << n << "): " << res.diagnostic << "\n";
                ++g_failures;
            }
        }
    }

    std::cout << "banks walked                                : " << banksWalked << "\n";
    std::cout << "records with a 'vorb' chunk                 : " << recordsWithVorb << "\n";
    std::cout << "reconstructed id+comment header accepted    : " << reconstructedHeaderAccepted << "\n";
    std::cout << "transcode failed before reaching libvorbis  : " << transcodeFailedBeforeLibvorbis << "\n";
    std::cout << "setup header rejected, exactly OV_EBADHEADER: " << setupRejectedExactlyBadHeader << "\n";
    std::cout << "setup header rejected, OTHER code (BAD)     : " << setupRejectedOtherCode << "\n";
    std::cout << "audio packet failed (setup OK, strict gate) : " << audioPacketFailed << "\n";
    std::cout << "real decode SUCCEEDED (ok==true)            : " << realDecodeSucceeded << "\n";
    std::cout << "transcode failure categories (this project's own wwise_setup_rebuild.cpp, not libvorbis):\n";
    for (const auto& kv : transcodeFailureReasons) {
        std::cout << "  " << kv.second << "x  " << kv.first << "\n";
    }
    std::cout << "audio-packet failure categories (rebuildWwiseAudioPacket()/vorbis_synthesis()):\n";
    for (const auto& kv : audioPacketFailureReasons) {
        std::cout << "  " << kv.second << "x  " << kv.first << "\n";
    }

    // Exact population counts, pinned (static shipped game data, not
    // expected to change - same practice real_zone_objects_test's own
    // header comment establishes for this project): 86 `_media.bnk_pc`
    // banks in cutscene_sounds.vpp_pc, 493 of their records carry a real
    // "vorb" chunk, every one of those 493 is a `ct_*`/`story_*` voice-line
    // or cutscene-score record (channels 1, 2 or 6 - checked above) this
    // project's own id/comment-header reconstruction accepts.
    CHECK(banksWalked == 86);
    CHECK(recordsWithVorb == 493);
    CHECK(reconstructedHeaderAccepted == recordsWithVorb);  // this project's OWN id/comment construction never fails
    CHECK(setupRejectedOtherCode == 0);  // every setup-stage failure is a categorized, expected outcome above

    // UPDATE 2026-10-09 (Wwise modified-packet rebuild task): added
    // `audioPacketFailed` to this invariant - a 4th real, categorized bucket
    // (setup header accepted, but the strict all-or-nothing audio-packet
    // gate rejected the record) that did not exist before this task's own
    // "drop the splice-survivors behavior" change. Still deliberately NOT
    // pinning any individual bucket's exact count (unlike banksWalked/
    // recordsWithVorb's static-shipped-data counts above) - only the
    // INVARIANT that every record lands in SOME accounted-for, categorized
    // bucket is checked here, so a future fix moving a record between
    // buckets is recognized as progress/change, not a broken pin.
    CHECK(transcodeFailedBeforeLibvorbis + setupRejectedExactlyBadHeader + audioPacketFailed + realDecodeSucceeded ==
          recordsWithVorb);

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "real_vorbis_decode_test: all checks passed\n";
    return 0;
#endif
}
