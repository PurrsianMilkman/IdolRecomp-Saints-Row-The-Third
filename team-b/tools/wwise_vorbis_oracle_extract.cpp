// DIAGNOSTIC-ONLY tool (2026-10-08, independent-oracle verification task,
// NOT a ctest - same "one-shot real-data probe" convention as every other
// tools/wwise_*_probe.cpp on this branch). Purpose: the orchestrator's own
// direct sample-statistics measurement of
// tests/golden/wwise_vorbis_decode/ct_kinzie_03_media_first_success.wav
// found a pattern (8.2% hard clipping, 32% exact-zero samples, RMS 10559,
// lag-1 autocorrelation 0.74, zero-crossing rate 0.16) inconsistent with
// correct dialogue/cutscene audio - i.e. the prior pass's "5/493 real
// decodes" claim was never checked against an INDEPENDENT oracle, only
// against this project's own internal plausibility gates (libvorbis
// accepting the rebuilt header, non-silence, duration). This tool produces
// the two things an independent check needs, for every one of the 5 records
// tests/real_vorbis_decode_test.cpp reports as ok==true:
//
//   1. this project's own decoded PCM, written as a standard WAV
//      (`ours_<name>.wav`) - the exact same attemptWwiseVorbisDecode() path
//      the real test exercises, not a re-derivation;
//   2. the RAW, untouched record bytes this project fed into that decode
//      (`raw_<name>.wem`) - the original Wwise-wrapped RIFF/"vorb" blob,
//      byte-for-byte, straight out of the real shipped bank, suitable as
//      input to a genuinely independent oracle decoder (vgmstream-cli,
//      which parses this exact container format itself, from its own
//      from-scratch implementation, not this project's code at all).
//
// Usage: wwise_vorbis_oracle_extract <path to cutscene_sounds.vpp_pc> <output dir>
//
// Nothing from the archive is copied into the repository - outDir is
// expected to be a scratch/working location outside this repo, same
// discipline as every other real-data diagnostic here.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sr3audio/media_bank.h"
#include "sr3audioruntime/real_vorbis_decode.h"
#include "sr3audioruntime/wav_writer.h"
#include "sr3audioruntime/wwise_vorbis_setup.h"
#include "vpp/container.h"

namespace {

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    if (n < 0) n = 0;
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}

bool endsWith(const std::string& n, const std::string& s) {
    return n.size() >= s.size() && n.compare(n.size() - s.size(), s.size(), s) == 0;
}

bool writeRawBlob(const std::string& path, const uint8_t* data, size_t size) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return f.good();
}

}  // namespace

int main(int argc, char** argv) {
    const std::string archivePath =
        argc > 1 ? argv[1] : "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache/cutscene_sounds.vpp_pc";
    const std::string outDir = argc > 2 ? argv[2] : ".";

    // Optional 4th argument (2026-10-09, broad-oracle-sweep integration task):
    // restrict processing to a single record, by name (substring match
    // against the output tag this tool already derives, e.g.
    // "ct_kinzie_03_media" or "ct_kinzie_03_media_rec1") or by its 0-based
    // index among records carrying a real "vorb" chunk (the exact same
    // population tests/real_vorbis_decode_test.cpp counts as
    // recordsWithVorb==493) - whichever the string parses as. Extends this
    // tool rather than adding a second one, per that task's own instruction;
    // default (argument omitted) keeps the prior all-qualifying-records
    // behavior unchanged.
    const std::string recordFilter = argc > 3 ? argv[3] : "";
    bool filterIsIndex = false;
    long long filterIndex = -1;
    if (!recordFilter.empty()) {
        try {
            size_t consumed = 0;
            long long parsed = std::stoll(recordFilter, &consumed);
            if (consumed == recordFilter.size()) {
                filterIsIndex = true;
                filterIndex = parsed;
            }
        } catch (const std::exception&) {
            // Not a plain integer - treat as a name substring instead.
        }
    }

    std::error_code mkec;
    std::filesystem::create_directories(outDir, mkec);

    std::vector<uint8_t> archiveBytes = readFile(archivePath);
    if (archiveBytes.empty()) {
        std::cerr << "could not read " << archivePath << "\n";
        return 1;
    }

    vpp::Container c(vpp::ByteView(archiveBytes.data(), archiveBytes.size()));

    long long recordsWithVorb = 0;
    long long okCount = 0;

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
        const size_t declaredSize = c.entries()[i].payload.decompressedLength;
        std::vector<sr3audio::MediaBankRecord> records;
        try {
            records = mb.walk(declaredSize);
        } catch (const std::exception&) {
            continue;
        }

        int recIdxInBank = -1;
        for (const auto& rec : records) {
            ++recIdxInBank;
            if (rec.size < 128) continue;
            const uint8_t* recBytes = bytes.data() + rec.offset;

            auto vorbLoc = sr3audioruntime::findRiffSubChunk(recBytes, rec.size, sr3audioruntime::kFourCcVorb);
            if (!vorbLoc) continue;
            sr3audioruntime::WwiseVorbChunk vorb =
                sr3audioruntime::parseVorbChunk(recBytes, rec.size, vorbLoc->first);
            if (!vorb.present) continue;
            const long long vorbIndex = recordsWithVorb;  // 0-based index of THIS record among recordsWithVorb
            ++recordsWithVorb;

            // Strip the trailing ".bnk_pc" for a clean base name; disambiguate
            // with the in-bank record index only if a bank ever holds more
            // than one record with a "vorb" chunk (not expected on this
            // population, but handled rather than silently overwritten).
            std::string base = n;
            const std::string suffix = ".bnk_pc";
            if (endsWith(base, suffix)) base = base.substr(0, base.size() - suffix.size());
            std::string tag = recIdxInBank == 0 ? base : base + "_rec" + std::to_string(recIdxInBank);

            if (!recordFilter.empty()) {
                const bool matches =
                    filterIsIndex ? (vorbIndex == filterIndex) : (tag.find(recordFilter) != std::string::npos);
                if (!matches) continue;
            }

            sr3audioruntime::DecodeAttemptResult res = sr3audioruntime::attemptWwiseVorbisDecode(recBytes, rec.size);
            if (!res.ok) {
                std::cout << "FAIL " << n << " (bank record #" << recIdxInBank << ", vorbIndex=" << vorbIndex
                           << ", tag='" << tag << "'): diagnostic: " << res.diagnostic << "\n";
                continue;
            }
            ++okCount;

            const std::string rawPath = outDir + "/raw_" + tag + ".wem";
            const std::string oursPath = outDir + "/ours_" + tag + ".wav";

            bool rawOk = writeRawBlob(rawPath, recBytes, rec.size);
            std::string wavErr;
            bool wavOk = sr3audioruntime::writeWavPcm16(oursPath, res.pcm, static_cast<uint32_t>(res.sampleRate),
                                                         static_cast<uint16_t>(res.channels), wavErr);

            std::cout << "OK  " << n << " (bank record #" << recIdxInBank << ", vorbIndex=" << vorbIndex
                      << ", tag='" << tag << "'): frames=" << res.totalSampleFrames
                      << " channels=" << res.channels << " sampleRate=" << res.sampleRate
                      << " rawBytes=" << rec.size
                      << " audioPacketsAttempted=" << res.audioPacketsTotal
                      << " audioPacketsDecodedOk=" << res.audioPacketsDecodedOk
                      << " -> raw='" << rawPath << "' (wrote=" << rawOk << ")"
                      << " ours='" << oursPath << "' (wrote=" << wavOk << (wavOk ? "" : (", err=" + wavErr)) << ")\n"
                      << "    diagnostic: " << res.diagnostic << "\n";
        }
    }

    std::cout << "records with a 'vorb' chunk : " << recordsWithVorb << "\n";
    std::cout << "real decode SUCCEEDED (ok)  : " << okCount << "\n";
    return 0;
}
