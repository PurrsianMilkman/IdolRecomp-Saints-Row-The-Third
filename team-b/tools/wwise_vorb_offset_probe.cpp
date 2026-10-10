// DIAGNOSTIC-ONLY tool (2026-10-08, vorb-chunk explicit-offset-field
// hypothesis task) - not part of the real decode path, not a ctest.
//
// RESULT: HYPOTHESIS REJECTED, population-wide, 3 distinct placements tried,
// all 0/493 plausible. See results/probe_vorb_explicit_offset_fields_
// 20261008.md for the full writeup; summary kept here so this tool's own
// top comment stays honest about what it actually found, matching this
// project's "no invented fixes / report exactly what was tried and why it
// failed" discipline.
//
// Tests a specific, narrower hypothesis than the general "leading per-
// packet seek/size table" finding commit 58e939f already left OPEN
// (results/probe_wwise_setup_packet_framing_20261008.md, include/
// sr3audioruntime/wwise_setup_rebuild.h's own 2026-10-08 UPDATE): per
// spec-audio-runtime.md Sec11.11.2's own table row -
//
//   "+0x7c/+0x80 (dwords) | vorb bytes 4-7, 8-11 | two offset-shaped fields
//   (vorb[4:8] small/near-constant, vorb[8:12] varies widely - consistent
//   with setup-packet-relative vs. first-audio-packet-offset); consumer
//   still untraced | CONFIRMED location... HIGH on exact semantic role"
//
// - the "vorb" chunk's own bytes [4:8) and [8:12) (as little-endian u32
// fields, read directly from WwiseVorbChunk::raw42, NOT from the already-
// empirically-relocated blockSizeExponent0/1 or sampleCountFallbackField
// positions this project's own wwise_vorbis_setup.h already corrected -
// those corrections are to DIFFERENT bytes, 0-3 and 40-41, and are
// unaffected by this probe) might be explicit setupPacketOffset/
// firstAudioPacketOffset fields. A port of ww2ogg (hcs64, BSD - xoreos's
// Sound::WwRIFFVorbisStream, a from-scratch C++ reimplementation whose own
// doxygen-generated class reference was read for this task; no ww2ogg or
// vgmstream SOURCE was read, copied, or transcribed, per this task's own
// hard rule) independently confirms a conceptually matching field PAIR
// really exists in the real public Wwise-Vorbis format under exactly these
// two names (`_setupPacketOffset`/`_firstAudioPacketOffset`) - but that
// doxygen page gives field NAMES only, no byte-level layout (no prose
// format-notes document with concrete byte offsets could be found within
// this task's own bound of "descriptive documentation... not source
// files" - the real layout lives only inside the converters' own source,
// which this task explicitly fenced off from this exploration path).
//
// Three placements tried against the real byte values, each using the
// EXISTING, already-validated probeSetupPacketBitBudget() as the
// falsifiable plausibility bar (small sane books/floors/residues/maps/
// modes counts, zero out-of-range codebook indices, <=7 leftover bits):
//   1. Literal: setup packet = bytes [fieldA, fieldB) relative to the start
//      of "data". REJECTED, 0/493 - the span is hundreds of KB (fieldB
//      tracks almost exactly with dataChunkSize itself), far too large for
//      a Vorbis setup packet; leftover bits in the millions.
//   2. Corrected-by-an-exact-population-wide-fact: this pass separately
//      found (dataChunkSize - fieldB) - cleanRunEnd == EXACTLY 4 for ALL
//      493/493 real records (zero variance, not a fuzzy percentage) -
//      cleanRunEnd being the independently-detected end of the prior
//      pass's own leading "clean/structured region." This exactly and
//      newly CONFIRMS (via a second, independent measurement - the vorb
//      chunk's own field, not just a byte-pattern heuristic) that there is
//      a real boundary inside "data" at trueTableEnd = dataChunkSize -
//      fieldB, superseding the prior pass's own heuristic estimate by a
//      now-pinned +4 byte correction. Testing setup packet = bytes
//      [fieldA, trueTableEnd) against that corrected boundary: still
//      REJECTED, 0/493 - fieldA (176-221, genuinely near-constant, matching
//      Sec11.11.2's "small" wording) lands INSIDE the same leading
//      structured region, not on real bitstream noise.
//   3. Combines two separately-confirmed facts rather than inventing a
//      third: Sec11.11.1's own CONFIRMED "bare 16-bit length prefix" packet
//      framing, applied starting exactly at the newly-pinned trueTableEnd
//      boundary. Still REJECTED, 0/493, though the closest of the three -
//      the declared length itself is suspiciously near-constant (174 bytes
//      for every mono record checked, 184 for the one 2-channel record
//      checked - a plausible SIZE for a minimal setup packet) and the
//      resulting books/floors/residues counts are small and sane-looking
//      rather than wild noise, but every record still hits EOF before
//      modes are reached (ranToEnd=false) - not a clean result.
//
// Net result: the exact trueTableEnd boundary is now a newly-CONFIRMED
// fact (not heuristic), real forward progress on the container's own
// structure - but WHERE inside [0, trueTableEnd) the real Vorbis setup
// packet's own bytes begin remains genuinely OPEN. Per "no invented fixes,"
// no fourth placement was guessed past this point. Relayed to Team A
// alongside the existing Sec11.11.1 spec-sync question.
//
// Usage: wwise_vorb_offset_probe <path to cutscene_sounds.vpp_pc>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "sr3audio/media_bank.h"
#include "sr3audioruntime/wwise_setup_rebuild.h"
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

uint32_t u32le(const std::vector<uint8_t>& raw42, size_t off) {
    return static_cast<uint32_t>(raw42[off]) | (static_cast<uint32_t>(raw42[off + 1]) << 8) |
           (static_cast<uint32_t>(raw42[off + 2]) << 16) | (static_cast<uint32_t>(raw42[off + 3]) << 24);
}

// Same "leading clean/structured region" detector the prior pass's
// wwise_setup_budget_probe.cpp already used (findCleanRunEnd) - reused here
// only as an independent cross-check landmark (does fieldB land on it?),
// not assumed to be the answer itself.
size_t findCleanRunEnd(const uint8_t* data, size_t dataChunkSize) {
    size_t entry = 0;
    for (;; ++entry) {
        size_t off = entry * 4 + 2;
        if (off + 4 > dataChunkSize) break;
        float f;
        std::memcpy(&f, data + off, 4);
        bool clean = false;
        if (std::isfinite(f) && std::fabs(f) > 0.05f && std::fabs(f) < 1.0e6f) {
            float r = std::round(f);
            if (std::fabs(f - r) / std::max(std::fabs(f), 1.0f) < 0.01f) clean = true;
        }
        if (!clean) break;
    }
    return entry * 4;
}

std::string hexDump(const uint8_t* p, size_t n) {
    static const char* hex = "0123456789abcdef";
    std::string s;
    s.reserve(n * 3);
    for (size_t i = 0; i < n; ++i) {
        s.push_back(hex[(p[i] >> 4) & 0xf]);
        s.push_back(hex[p[i] & 0xf]);
        s.push_back(' ');
    }
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string archivePath = argc > 1 ? argv[1] : "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache/cutscene_sounds.vpp_pc";

    std::vector<uint8_t> archiveBytes = readFile(archivePath);
    if (archiveBytes.empty()) {
        std::cerr << "could not read " << archivePath << "\n";
        return 1;
    }

    vpp::Container c(vpp::ByteView(archiveBytes.data(), archiveBytes.size()));

    long long recordsWithVorb = 0;
    long long fieldAWithinData = 0, fieldBWithinData = 0;     // 0 <= field <= dataChunkSize
    long long fieldALessThanFieldB = 0;                        // ordering sanity
    long long fieldBEqualsCleanRunEnd = 0;                     // cross-check vs. the independently-found table
    long long fieldAIsZero = 0;
    long long nearConstantFieldA = 0;                          // fieldA within [0, 64) - "small" per spec wording

    std::map<uint32_t, long long> fieldAHistogram;  // is fieldA really "near-constant" across records?
    uint64_t fieldAMin = UINT64_MAX, fieldAMax = 0, fieldASum = 0;
    uint64_t fieldBMin = UINT64_MAX, fieldBMax = 0, fieldBSum = 0;

    // The actual pass/fail criterion this task's dispatch asked for: after
    // reading fieldA as a setup-packet offset (relative to the start of
    // "data") and fieldB as a first-audio-packet offset (same base), does
    // the byte range [fieldA, fieldB) - fed to the EXISTING, already-
    // validated probeSetupPacketBitBudget() - parse as a plausible Vorbis
    // setup packet (small books/floors/residues/maps/modes counts, no
    // wildly-out-of-range codebook indices, and the range closes with <=7
    // leftover bits)? This is the same falsifiable bar commit 58e939f's own
    // probe already used for the two REJECTED placements - applied here to
    // a THIRD, new placement.
    long long plausibleSetupParse = 0;
    long long printedExamples = 0;
    long long plausibleSetupParseCorrected = 0;
    long long printedExamplesCorrected = 0;
    long long plausibleSetupParseLenPrefixed = 0;
    long long printedExamplesLenPrefixed = 0;
    long long lenPrefixOutOfBounds = 0;

    // Secondary, population-wide observation (NOT exploited as a fix - just
    // measured honestly): does (dataChunkSize - fieldB) track the
    // independently-found leading "clean/structured region" length
    // (cleanRunEnd, the same landmark commit 58e939f's own probe used)?
    // If fieldB consistently sits a small, roughly-cleanRunEnd-sized gap
    // BEFORE the end of "data" rather than shortly AFTER a setup packet
    // near the FRONT, that is real evidence against the literal
    // "first-audio-packet-offset" semantic, independent of the plausibility
    // check above.
    long long deltaCloseToCleanRunEnd = 0;  // |（dataChunkSize-fieldB) - cleanRunEnd| <= 32
    int64_t deltaMin = INT64_MAX, deltaMax = INT64_MIN;
    double deltaSum = 0.0;

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

        for (const auto& rec : records) {
            if (rec.size < 128) continue;
            const uint8_t* recBytes = bytes.data() + rec.offset;

            auto vorbLoc = sr3audioruntime::findRiffSubChunk(recBytes, rec.size, sr3audioruntime::kFourCcVorb);
            if (!vorbLoc) continue;
            sr3audioruntime::WwiseVorbChunk vorb = sr3audioruntime::parseVorbChunk(recBytes, rec.size, vorbLoc->first);
            if (!vorb.present) continue;

            auto chunks = sr3audioruntime::parseRiffWaveContainer(recBytes, rec.size);
            if (!chunks.valid || !chunks.hasFmt || !chunks.hasData) continue;
            sr3audioruntime::StandardWaveFmt fmt =
                sr3audioruntime::parseStandardWaveFmt(recBytes, chunks.fmtOffset, chunks.fmtSize);
            if (!fmt.present || fmt.channels == 0) continue;

            ++recordsWithVorb;

            uint32_t fieldA = u32le(vorb.raw42, 4);   // vorb[4:8)
            uint32_t fieldB = u32le(vorb.raw42, 8);   // vorb[8:12)

            fieldAMin = std::min<uint64_t>(fieldAMin, fieldA);
            fieldAMax = std::max<uint64_t>(fieldAMax, fieldA);
            fieldASum += fieldA;
            fieldBMin = std::min<uint64_t>(fieldBMin, fieldB);
            fieldBMax = std::max<uint64_t>(fieldBMax, fieldB);
            fieldBSum += fieldB;
            ++fieldAHistogram[fieldA];

            if (fieldA == 0) ++fieldAIsZero;
            if (fieldA < 64) ++nearConstantFieldA;
            if (static_cast<size_t>(fieldA) <= chunks.dataSize) ++fieldAWithinData;
            if (static_cast<size_t>(fieldB) <= chunks.dataSize) ++fieldBWithinData;
            if (fieldA < fieldB) ++fieldALessThanFieldB;

            size_t cleanEnd = findCleanRunEnd(recBytes + chunks.dataOffset, chunks.dataSize);
            if (fieldB == cleanEnd) ++fieldBEqualsCleanRunEnd;

            int64_t trailingGap = static_cast<int64_t>(chunks.dataSize) - static_cast<int64_t>(fieldB);
            int64_t delta = trailingGap - static_cast<int64_t>(cleanEnd);
            deltaMin = std::min(deltaMin, delta);
            deltaMax = std::max(deltaMax, delta);
            deltaSum += static_cast<double>(delta);
            if (std::llabs(delta) <= 32) ++deltaCloseToCleanRunEnd;

            // The real pass/fail check: parse [dataOffset+fieldA, dataOffset+fieldB)
            // as a setup packet via the EXISTING validated probe.
            if (fieldA < fieldB && static_cast<size_t>(fieldB) <= chunks.dataSize) {
                size_t setupLen = static_cast<size_t>(fieldB - fieldA);
                sr3audioruntime::SetupBudgetProbeResult probe = sr3audioruntime::probeSetupPacketBitBudget(
                    recBytes + chunks.dataOffset + fieldA, setupLen, fmt.channels);
                bool plausible = probe.ranToEnd && probe.leftoverBits >= 0 && probe.leftoverBits <= 7 &&
                                  probe.outOfRangeCount == 0 && probe.books >= 1 && probe.books <= 256 &&
                                  probe.floors >= 1 && probe.floors <= 64 && probe.residues >= 1 &&
                                  probe.residues <= 64 && probe.maps >= 1 && probe.maps <= 64 && probe.modes >= 1 &&
                                  probe.modes <= 64;
                if (plausible) ++plausibleSetupParse;

                if (printedExamples < 6) {
                    ++printedExamples;
                    std::cout << "[" << n << "] rec@" << rec.offset << " channels=" << fmt.channels
                              << " dataChunkSize=" << chunks.dataSize << " vorb.sampleCountFallback="
                              << vorb.sampleCountFallbackField << "\n";
                    std::cout << "    raw42 hex: " << hexDump(vorb.raw42.data(), 42) << "\n";
                    std::cout << "    fieldA(vorb[4:8))=" << fieldA << " fieldB(vorb[8:12))=" << fieldB
                              << " (fieldB-fieldA)=" << (fieldB - fieldA) << " cleanRunEnd=" << cleanEnd << "\n";
                    std::cout << "    PARSE[fieldA,fieldB): books=" << probe.books << " floors=" << probe.floors
                              << " residues=" << probe.residues << " maps=" << probe.maps << " modes=" << probe.modes
                              << " outOfRangeCount=" << probe.outOfRangeCount << " ranToEnd=" << probe.ranToEnd
                              << " leftoverBits=" << probe.leftoverBits << " PLAUSIBLE=" << plausible << "\n";
                }
            }

            // CORRECTED placement, motivated by the exact delta==4
            // population-wide finding above (not a blind guess): reinterpret
            // fieldB as a trailing BYTE COUNT measured from the END of
            // "data" (dataChunkSize - fieldB), not an absolute offset from
            // its START. trueTableEnd = dataChunkSize - fieldB. Test
            // [fieldA, trueTableEnd) as the setup packet instead.
            if (static_cast<size_t>(fieldB) <= chunks.dataSize) {
                size_t trueTableEnd = chunks.dataSize - fieldB;
                if (fieldA < trueTableEnd) {
                    size_t setupLen2 = trueTableEnd - fieldA;
                    sr3audioruntime::SetupBudgetProbeResult probe2 = sr3audioruntime::probeSetupPacketBitBudget(
                        recBytes + chunks.dataOffset + fieldA, setupLen2, fmt.channels);
                    bool plausible2 = probe2.ranToEnd && probe2.leftoverBits >= 0 && probe2.leftoverBits <= 7 &&
                                       probe2.outOfRangeCount == 0 && probe2.books >= 1 && probe2.books <= 256 &&
                                       probe2.floors >= 1 && probe2.floors <= 64 && probe2.residues >= 1 &&
                                       probe2.residues <= 64 && probe2.maps >= 1 && probe2.maps <= 64 &&
                                       probe2.modes >= 1 && probe2.modes <= 64;
                    if (plausible2) ++plausibleSetupParseCorrected;
                    if (printedExamplesCorrected < 6) {
                        ++printedExamplesCorrected;
                        std::cout << "  [CORRECTED] [" << n << "] rec@" << rec.offset << " trueTableEnd="
                                  << trueTableEnd << " setupLen2=" << setupLen2 << "\n";
                        std::cout << "    PARSE[fieldA,trueTableEnd): books=" << probe2.books
                                  << " floors=" << probe2.floors << " residues=" << probe2.residues
                                  << " maps=" << probe2.maps << " modes=" << probe2.modes
                                  << " outOfRangeCount=" << probe2.outOfRangeCount
                                  << " ranToEnd=" << probe2.ranToEnd << " leftoverBits=" << probe2.leftoverBits
                                  << " PLAUSIBLE=" << plausible2 << "\n";
                        std::cout << "    setup bytes first 32: "
                                  << hexDump(recBytes + chunks.dataOffset + fieldA, std::min<size_t>(32, setupLen2))
                                  << "\n";
                    }
                }
            }

            // THIRD test: combine two separately-evidenced facts rather than
            // inventing a new placement - Sec11.11.1's own CONFIRMED "bare
            // 16-bit little-endian length prefix" packet framing, applied
            // starting exactly at trueTableEnd (this pass's new, exact,
            // population-wide-zero-variance boundary) instead of byte 0 of
            // "data" (now known wrong - that lands mid-table).
            if (static_cast<size_t>(fieldB) <= chunks.dataSize) {
                size_t trueTableEnd = chunks.dataSize - fieldB;
                if (trueTableEnd + 2 <= chunks.dataSize) {
                    const uint8_t* p = recBytes + chunks.dataOffset + trueTableEnd;
                    uint16_t lenPrefix = static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
                    if (trueTableEnd + 2 + lenPrefix <= chunks.dataSize && lenPrefix > 0) {
                        sr3audioruntime::SetupBudgetProbeResult probe3 = sr3audioruntime::probeSetupPacketBitBudget(
                            p + 2, lenPrefix, fmt.channels);
                        bool plausible3 = probe3.ranToEnd && probe3.leftoverBits >= 0 && probe3.leftoverBits <= 7 &&
                                           probe3.outOfRangeCount == 0 && probe3.books >= 1 && probe3.books <= 256 &&
                                           probe3.floors >= 1 && probe3.floors <= 64 && probe3.residues >= 1 &&
                                           probe3.residues <= 64 && probe3.maps >= 1 && probe3.maps <= 64 &&
                                           probe3.modes >= 1 && probe3.modes <= 64;
                        if (plausible3) ++plausibleSetupParseLenPrefixed;
                        if (printedExamplesLenPrefixed < 6) {
                            ++printedExamplesLenPrefixed;
                            std::cout << "  [LENPREFIX@trueTableEnd] [" << n << "] rec@" << rec.offset
                                      << " trueTableEnd=" << trueTableEnd << " lenPrefix=" << lenPrefix << "\n";
                            std::cout << "    PARSE: books=" << probe3.books << " floors=" << probe3.floors
                                      << " residues=" << probe3.residues << " maps=" << probe3.maps
                                      << " modes=" << probe3.modes << " outOfRangeCount=" << probe3.outOfRangeCount
                                      << " ranToEnd=" << probe3.ranToEnd << " leftoverBits=" << probe3.leftoverBits
                                      << " PLAUSIBLE=" << plausible3 << "\n";
                        }
                    } else {
                        ++lenPrefixOutOfBounds;
                    }
                }
            }
        }
    }

    std::cout << "\n=== POPULATION SUMMARY (" << recordsWithVorb << " real records) ===\n";
    std::cout << "fieldA (vorb[4:8)) range: [" << fieldAMin << ", " << fieldAMax << "] mean="
              << (recordsWithVorb ? static_cast<double>(fieldASum) / recordsWithVorb : 0.0) << "\n";
    std::cout << "fieldB (vorb[8:12)) range: [" << fieldBMin << ", " << fieldBMax << "] mean="
              << (recordsWithVorb ? static_cast<double>(fieldBSum) / recordsWithVorb : 0.0) << "\n";
    std::cout << "distinct fieldA values seen: " << fieldAHistogram.size() << "\n";
    std::cout << "fieldA == 0                                  : " << fieldAIsZero << "/" << recordsWithVorb << "\n";
    std::cout << "fieldA < 64 (\"small\" per spec wording)        : " << nearConstantFieldA << "/" << recordsWithVorb << "\n";
    std::cout << "fieldA <= dataChunkSize                       : " << fieldAWithinData << "/" << recordsWithVorb << "\n";
    std::cout << "fieldB <= dataChunkSize                       : " << fieldBWithinData << "/" << recordsWithVorb << "\n";
    std::cout << "fieldA < fieldB                                : " << fieldALessThanFieldB << "/" << recordsWithVorb << "\n";
    std::cout << "fieldB == independently-found cleanRunEnd      : " << fieldBEqualsCleanRunEnd << "/" << recordsWithVorb << "\n";
    std::cout << "PARSE([dataOffset+fieldA, dataOffset+fieldB)) PLAUSIBLE as a setup packet: " << plausibleSetupParse
              << "/" << recordsWithVorb << "\n";
    std::cout << "\n(dataChunkSize-fieldB) vs. independently-found cleanRunEnd - delta stats:\n";
    std::cout << "  delta=(dataChunkSize-fieldB)-cleanRunEnd: min=" << deltaMin << " max=" << deltaMax
              << " mean=" << (recordsWithVorb ? deltaSum / recordsWithVorb : 0.0) << "\n";
    std::cout << "  |delta|<=32 bytes: " << deltaCloseToCleanRunEnd << "/" << recordsWithVorb << "\n";
    std::cout << "\nCORRECTED PARSE([dataOffset+fieldA, dataOffset+(dataChunkSize-fieldB))) PLAUSIBLE: "
              << plausibleSetupParseCorrected << "/" << recordsWithVorb << "\n";
    std::cout << "LENPREFIX@trueTableEnd PLAUSIBLE: " << plausibleSetupParseLenPrefixed << "/" << recordsWithVorb
              << " (out-of-bounds length prefix: " << lenPrefixOutOfBounds << ")\n";

    std::cout << "\ntop fieldA values (value: occurrence count) - is it really near-constant?\n";
    {
        std::vector<std::pair<uint32_t, long long>> sorted(fieldAHistogram.begin(), fieldAHistogram.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        for (size_t i = 0; i < sorted.size() && i < 15; ++i) std::cout << "  " << sorted[i].first << ": " << sorted[i].second << "\n";
    }

    return 0;
}
