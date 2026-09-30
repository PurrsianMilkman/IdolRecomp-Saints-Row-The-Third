// Measures ONE thing across the whole zone population: where the Mesh
// sub-block's 0x70-byte header actually starts, relative to its `u32 == 9`
// version field.
//
// WHY. sr3mesh/mesh_block.h fixes kHeaderStart = 0x10 - version, check
// value, c-length, g-length, then the header - measured on 400 real paired
// CHARACTER/ITEM meshes. Examining one member of HANDOFF §9.55.1 Gap 2's
// "no g-backed anchor candidate" bucket by hand (dlc2 :: sr3_city~sdlc2_
// gis_o!^dlc2_go, anchor at czn+0xdc) showed every pre-header field in its
// documented place but the HEADER four bytes later than kHeaderStart says,
// with a zero word filling the gap. That is a single file and an eyeball;
// this tool turns it into a population measurement with a control.
//
// METHOD. Anchors are located by the REVERSE test, not by the flags byte:
// spec-zone-data-format.md §7.1 confirms (1,002/1,002) that a `.gzn_pc`
// segment opens on the block's own check value, so a 4-aligned `u32 == 9`
// whose following u32 appears at a 16-aligned offset in the paired
// `.gzn_pc` is an anchor on evidence that is independent of any header
// field. For each such anchor, the FULL g-segment walk (index buffer, then
// each channel's data at its declared 16-aligned cursor, then the exact
// `cursor + 4 == gLength` landing and the trailing check-value bookend -
// the same contract sr3mesh::MeshBlock::parse enforces) is replayed with
// the header assumed at pos+d for each candidate displacement d, and the
// set of d values that produce a fully consistent walk is recorded.
//
// The control is built in: d = 0x10 is the shipped reader's own value, so
// the pass population must come back overwhelmingly d=0x10, and any d that
// "validates" for a large share of BOTH populations would show the test is
// too weak to distinguish anything. Ambiguity (more than one d validating
// for the same anchor) is counted rather than resolved silently.
//
// Read-only. Nothing in src/ is called differently from production and
// nothing here feeds back into the shipped reader.
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "sr3mesh/mesh_block.h"
#include "sr3zone/zone_geometry.h"
#include "vpp/container.h"

namespace {

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n > 0 ? n : 0));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}

bool stemFor(const std::string& n, const std::string& ext, std::string& stem) {
    if (n.size() > ext.size() && n.compare(n.size() - ext.size(), ext.size(), ext) == 0) {
        stem = n.substr(0, n.size() - ext.size());
        return true;
    }
    return false;
}

uint32_t u32at(const uint8_t* d, size_t p) {
    return static_cast<uint32_t>(d[p]) | (static_cast<uint32_t>(d[p + 1]) << 8) |
           (static_cast<uint32_t>(d[p + 2]) << 16) | (static_cast<uint32_t>(d[p + 3]) << 24);
}

size_t alignUp(size_t v, size_t a) { return (v + a - 1) / a * a; }

std::string hex32(uint32_t v) {
    char b[16];
    snprintf(b, sizeof(b), "0x%08X", v);
    return b;
}

bool getEntry(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    bool ok = r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
              r.status == vpp::DecodeStatus::ContentValidated ||
              r.status == vpp::DecodeStatus::RecoveredSharedStream ||
              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    out = std::move(r.data);
    return true;
}

// Replay of the g-segment contract with the header assumed at pos + d.
// Returns true only if every step is consistent AND the walk lands exactly
// on gLength with the trailing check-value bookend in place.
bool walkValidatesAt(const std::vector<uint8_t>& cb, size_t pos, size_t d, const std::vector<uint8_t>& gb,
                     size_t gOff, uint32_t checkValue, uint32_t gLength, bool& flagBit0Out,
                     uint32_t& channelCountOut, uint32_t& indexCountOut, bool requireBookend = true) {
    flagBit0Out = false;
    channelCountOut = 0;
    indexCountOut = 0;
    const size_t header = pos + d;
    if (header + sr3mesh::kHeaderSize > cb.size()) return false;

    const uint8_t flags = cb[header + sr3mesh::kFlagsOffset];
    flagBit0Out = (flags & sr3mesh::kFlagBulkInGFile) != 0;
    if (flags & sr3mesh::kFlagMultiStream) return false;

    const uint32_t channelCount = u32at(cb.data(), header + sr3mesh::kChannelCountOffset);
    const uint32_t indexCount = u32at(cb.data(), header + sr3mesh::kIndexCountOffset);
    const uint8_t indexElemSize = cb[header + sr3mesh::kIndexSizeOffset];
    channelCountOut = channelCount;
    indexCountOut = indexCount;
    if (channelCount == 0 || channelCount > 64) return false;
    if (indexElemSize != 2 && indexElemSize != 4) return false;

    const size_t recordsAt = header + sr3mesh::kHeaderSize;
    if (recordsAt + static_cast<size_t>(channelCount) * sr3mesh::kChannelRecordSize > cb.size()) return false;
    if (gOff + gLength > gb.size()) return false;
    if (gLength < 8) return false;

    if (u32at(gb.data(), gOff) != checkValue) return false;

    size_t cursor = alignUp(4, 16);
    size_t indexBytes = static_cast<size_t>(indexCount) * indexElemSize;
    if (cursor + indexBytes > gLength) return false;
    cursor += indexBytes;

    for (uint32_t ci = 0; ci < channelCount; ++ci) {
        const size_t at = recordsAt + static_cast<size_t>(ci) * sr3mesh::kChannelRecordSize;
        const uint32_t elementCount = u32at(cb.data(), at + 0x00);
        const size_t stride = static_cast<size_t>(cb[at + 0x04]) + cb[at + 0x07];
        if (stride == 0) return false;
        cursor = alignUp(cursor, 16);
        const size_t bytes = static_cast<size_t>(elementCount) * stride;
        if (cursor + bytes > gLength) return false;
        cursor += bytes;
    }
    cursor = alignUp(cursor, 4);
    if (!requireBookend) {
        // RELAXED variant: the declared g-length covers the walked content only,
        // with no trailing check-value bookend inside it. Measured separately -
        // never mixed into the strict tally - because a looser contract accepts
        // strictly more and would otherwise inflate every number it touches.
        return cursor == gLength;
    }
    if (cursor + 4 != gLength) return false;
    return u32at(gb.data(), gOff + cursor) == checkValue;
}

// SECOND, INDEPENDENT ORACLE for the header displacement.
//
// Each 24-byte channel record carries a u32 at record+0x10 that, on every
// block examined by hand, equals the 16-aligned cursor the segment walk
// computes for that channel - a DECLARED data offset. src/mesh_block.cpp
// never reads it (it computes `channel.dataOffset` from the walk instead),
// so it is untouched by whatever the walk assumes and can referee the
// displacement without reference to the trailing bookend or the declared
// g-length at all. Agreement across every channel of a block is the test.
bool declaredOffsetsAgreeAt(const std::vector<uint8_t>& cb, size_t pos, size_t d, uint32_t& channelsOut) {
    channelsOut = 0;
    const size_t header = pos + d;
    if (header + sr3mesh::kHeaderSize > cb.size()) return false;
    const uint32_t channelCount = u32at(cb.data(), header + sr3mesh::kChannelCountOffset);
    const uint32_t indexCount = u32at(cb.data(), header + sr3mesh::kIndexCountOffset);
    const uint8_t indexElemSize = cb[header + sr3mesh::kIndexSizeOffset];
    if (channelCount == 0 || channelCount > 64) return false;
    if (indexElemSize != 2 && indexElemSize != 4) return false;
    const size_t recordsAt = header + sr3mesh::kHeaderSize;
    if (recordsAt + static_cast<size_t>(channelCount) * sr3mesh::kChannelRecordSize > cb.size()) return false;

    size_t cursor = alignUp(4, 16) + static_cast<size_t>(indexCount) * indexElemSize;
    for (uint32_t ci = 0; ci < channelCount; ++ci) {
        const size_t at = recordsAt + static_cast<size_t>(ci) * sr3mesh::kChannelRecordSize;
        const uint32_t elementCount = u32at(cb.data(), at + 0x00);
        const size_t stride = static_cast<size_t>(cb[at + 0x04]) + cb[at + 0x07];
        if (stride == 0) return false;
        cursor = alignUp(cursor, 16);
        if (u32at(cb.data(), at + 0x10) != cursor) return false;
        cursor += static_cast<size_t>(elementCount) * stride;
    }
    channelsOut = channelCount;
    return true;
}

// THIRD variant: take each channel's data offset from the record's OWN
// declared field (record+0x10) instead of re-deriving it with the
// align-16 cursor rule, then require the same closing contract (walk ends
// exactly 4 bytes short of the declared g-length, trailing check-value
// bookend present). This is the project's standing lesson - when a
// structure declares its own size or position, read it - applied as a
// hypothesis test, not as a change to the shipped reader. Tallied
// separately from both contracts above.
bool walkWithDeclaredOffsets(const std::vector<uint8_t>& cb, size_t pos, size_t d,
                             const std::vector<uint8_t>& gb, size_t gOff, uint32_t checkValue,
                             uint32_t gLength) {
    const size_t header = pos + d;
    if (header + sr3mesh::kHeaderSize > cb.size()) return false;
    const uint8_t flags = cb[header + sr3mesh::kFlagsOffset];
    if (flags & sr3mesh::kFlagMultiStream) return false;
    const uint32_t channelCount = u32at(cb.data(), header + sr3mesh::kChannelCountOffset);
    const uint32_t indexCount = u32at(cb.data(), header + sr3mesh::kIndexCountOffset);
    const uint8_t indexElemSize = cb[header + sr3mesh::kIndexSizeOffset];
    if (channelCount == 0 || channelCount > 64) return false;
    if (indexElemSize != 2 && indexElemSize != 4) return false;
    const size_t recordsAt = header + sr3mesh::kHeaderSize;
    if (recordsAt + static_cast<size_t>(channelCount) * sr3mesh::kChannelRecordSize > cb.size()) return false;
    if (gOff + gLength > gb.size() || gLength < 8) return false;
    if (u32at(gb.data(), gOff) != checkValue) return false;

    size_t minStart = alignUp(4, 16) + static_cast<size_t>(indexCount) * indexElemSize;
    size_t end = 0;
    for (uint32_t ci = 0; ci < channelCount; ++ci) {
        const size_t at = recordsAt + static_cast<size_t>(ci) * sr3mesh::kChannelRecordSize;
        const uint32_t elementCount = u32at(cb.data(), at + 0x00);
        const size_t stride = static_cast<size_t>(cb[at + 0x04]) + cb[at + 0x07];
        if (stride == 0) return false;
        const size_t declared = u32at(cb.data(), at + 0x10);
        if (declared < minStart) return false;                 // would overlap the index buffer
        const size_t bytes = static_cast<size_t>(elementCount) * stride;
        if (declared + bytes > gLength) return false;
        minStart = declared + bytes;
        end = declared + bytes;
    }
    end = alignUp(end, 4);
    if (end + 4 != gLength) return false;
    return u32at(gb.data(), gOff + end) == checkValue;
}

const size_t kDisplacements[] = {0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20};
constexpr size_t kNumD = sizeof(kDisplacements) / sizeof(kDisplacements[0]);

// --- tallies -------------------------------------------------------------
long long g_pairs = 0, g_pairsPass = 0, g_pairsFail = 0;
long long g_blocksFromLocate = 0;
long long g_anchors = 0, g_anchorsPass = 0, g_anchorsFail = 0;
std::map<size_t, long long> g_dHistPass, g_dHistFail;       // first validating d
long long g_ambiguousPass = 0, g_ambiguousFail = 0;         // >1 validating d for one anchor
long long g_noDPass = 0, g_noDFail = 0;                     // no d validated
std::map<size_t, long long> g_dHistRelaxedPass, g_dHistRelaxedFail; // strict-rejected anchors, looser contract
long long g_relaxedNonePass = 0, g_relaxedNoneFail = 0;
std::map<size_t, long long> g_agreeHistPass, g_agreeHistFail;
long long g_agreeNonePass = 0, g_agreeNoneFail = 0;
long long g_agreeAmbigPass = 0, g_agreeAmbigFail = 0;
std::map<std::string, long long> g_strictVsAgree;
std::map<size_t, long long> g_declHistPass, g_declHistFail;
long long g_declNonePass = 0, g_declNoneFail = 0;
std::map<std::string, long long> g_strictVsDeclWalk;
// Cross-tab: production flags-bit-0 peek at pos+0x10 vs the d that actually works.
std::map<std::string, long long> g_flagsVsD;
// cLength self-consistency: does u32 at pos + cLength - 4 equal the check value?
long long g_cLenBookendOkPass = 0, g_cLenBookendOkFail = 0;

struct FailFileRow {
    std::string container, stem;
    size_t cznSize = 0, gznSize = 0;
    long long anchors = 0;
    std::map<size_t, long long> dHist;
    long long noD = 0;
    std::string firstAnchorDetail;
};
std::vector<FailFileRow> g_failRows;
long long g_failFilesFullyExplainedBy0x14 = 0;
long long g_failFilesWithNoAnchorAtAll = 0;

std::vector<std::string> g_examples;
std::vector<std::string> g_passNoDExamples;

void process(const std::string& container, const std::string& stem, const std::vector<uint8_t>& cb,
             const std::vector<uint8_t>& gb) {
    ++g_pairs;
    const size_t producedByLocate =
        sr3zone::ZoneGeometry::locate(sr3zone::ByteView(cb.data(), cb.size()),
                                      sr3zone::ByteView(gb.data(), gb.size()))
            .size();
    const bool pass = producedByLocate != 0;
    g_blocksFromLocate += static_cast<long long>(producedByLocate);
    if (pass) ++g_pairsPass; else ++g_pairsFail;

    std::unordered_map<uint32_t, std::vector<size_t>> gIndex;
    for (size_t p = 0; p + 4 <= gb.size(); p += 16) gIndex[u32at(gb.data(), p)].push_back(p);

    FailFileRow row;
    row.container = container;
    row.stem = stem;
    row.cznSize = cb.size();
    row.gznSize = gb.size();

    for (size_t pos = 0; pos + 16 <= cb.size(); pos += 4) {
        if (u32at(cb.data(), pos) != sr3mesh::kMeshVersion) continue;
        const uint32_t cv = u32at(cb.data(), pos + sr3mesh::kCheckValueOffset);
        // spec-zone-data-format.md §7.3: check values are hash-like and >= 0x10000.
        // Without this bound, `9` followed by a zero word matches the (always
        // present) zero entries of gIndex and floods the anchor set with
        // coincidences - measured directly: it turned 80 real anchors into 370.
        if (cv < 0x10000) continue;
        auto it = gIndex.find(cv);
        if (it == gIndex.end()) continue; // not an anchor by the independent g-file oracle

        const uint32_t cLength = u32at(cb.data(), pos + sr3mesh::kCLengthOffset);
        const uint32_t gLength = u32at(cb.data(), pos + sr3mesh::kGLengthOffset);

        ++g_anchors;
        ++row.anchors;
        if (pass) ++g_anchorsPass; else ++g_anchorsFail;

        if (cLength >= 8 && pos + cLength <= cb.size() && u32at(cb.data(), pos + cLength - 4) == cv) {
            if (pass) ++g_cLenBookendOkPass; else ++g_cLenBookendOkFail;
        }

        std::vector<size_t> validD;
        bool flagAt0x10 = (pos + 0x10 < cb.size()) && (cb[pos + 0x10] & sr3mesh::kFlagBulkInGFile) != 0;
        uint32_t cc = 0, ic = 0;
        size_t chosenGOff = 0;
        for (size_t di = 0; di < kNumD; ++di) {
            const size_t d = kDisplacements[di];
            bool okAny = false;
            for (size_t gOff : it->second) {
                bool fb = false;
                uint32_t c2 = 0, i2 = 0;
                if (walkValidatesAt(cb, pos, d, gb, gOff, cv, gLength, fb, c2, i2)) {
                    okAny = true;
                    if (validD.empty()) { cc = c2; ic = i2; chosenGOff = gOff; }
                    break;
                }
            }
            if (okAny) validD.push_back(d);
        }

        // Declared-offset walk variant, tallied for every anchor.
        std::vector<size_t> declD;
        for (size_t di = 0; di < kNumD; ++di) {
            for (size_t gOff : it->second) {
                if (walkWithDeclaredOffsets(cb, pos, kDisplacements[di], gb, gOff, cv, gLength)) {
                    declD.push_back(kDisplacements[di]);
                    break;
                }
            }
            if (!declD.empty()) break;
        }
        if (declD.empty()) { if (pass) ++g_declNonePass; else ++g_declNoneFail; }
        else { if (pass) ++g_declHistPass[declD[0]]; else ++g_declHistFail[declD[0]]; }
        {
            std::string k = "strictD=" + (validD.empty() ? std::string("none") : hex32((uint32_t)validD[0])) +
                            " / declaredOffsetWalkD=" +
                            (declD.empty() ? std::string("none") : hex32((uint32_t)declD[0]));
            ++g_strictVsDeclWalk[k];
        }

        // Independent oracle, computed for every anchor regardless of the
        // strict/relaxed outcome, and cross-tabbed against it below.
        std::vector<size_t> agreeD;
        for (size_t di = 0; di < kNumD; ++di) {
            uint32_t nch = 0;
            if (declaredOffsetsAgreeAt(cb, pos, kDisplacements[di], nch)) agreeD.push_back(kDisplacements[di]);
        }
        if (agreeD.empty()) { if (pass) ++g_agreeNonePass; else ++g_agreeNoneFail; }
        else {
            if (pass) ++g_agreeHistPass[agreeD[0]]; else ++g_agreeHistFail[agreeD[0]];
            if (agreeD.size() > 1) { if (pass) ++g_agreeAmbigPass; else ++g_agreeAmbigFail; }
        }
        {
            std::string k = "strictD=" + (validD.empty() ? std::string("none") : hex32((uint32_t)validD[0])) +
                            " / declaredOffsetD=" +
                            (agreeD.empty() ? std::string("none") : hex32((uint32_t)agreeD[0]));
            ++g_strictVsAgree[k];
        }

        std::string flagsKey = std::string(flagAt0x10 ? "flags@+0x10 bit0 SET" : "flags@+0x10 bit0 CLEAR") +
                               " / d=" + (validD.empty() ? std::string("none") : hex32((uint32_t)validD[0]));
        ++g_flagsVsD[flagsKey];

        if (validD.empty()) {
            if (pass) ++g_noDPass; else ++g_noDFail;
            ++row.noD;
            // Secondary, strictly-looser contract, tallied separately: does the
            // walk close if the declared g-length covers the content only, with
            // no trailing bookend inside it? Only ever consulted for anchors the
            // strict contract already rejected, so it can never inflate the
            // strict numbers above.
            for (size_t di = 0; di < kNumD; ++di) {
                bool okAny = false;
                for (size_t gOff : it->second) {
                    bool fb = false;
                    uint32_t c2 = 0, i2 = 0;
                    if (walkValidatesAt(cb, pos, kDisplacements[di], gb, gOff, cv, gLength, fb, c2, i2,
                                        /*requireBookend=*/false)) { okAny = true; break; }
                }
                if (okAny) {
                    if (pass) ++g_dHistRelaxedPass[kDisplacements[di]];
                    else ++g_dHistRelaxedFail[kDisplacements[di]];
                    break;
                }
                if (di + 1 == kNumD) { if (pass) ++g_relaxedNonePass; else ++g_relaxedNoneFail; }
            }
            if (pass && g_passNoDExamples.size() < 12) {
                // Why does a real-looking anchor in a PASSING file validate at no
                // d? Recorded rather than assumed - if this replay is rejecting
                // blocks production accepts, every number above is suspect.
                bool fb = false;
                uint32_t c2 = 0, i2 = 0;
                walkValidatesAt(cb, pos, 0x10, gb, it->second[0], cv, gLength, fb, c2, i2);
                char buf[512];
                snprintf(buf, sizeof(buf),
                         "%s :: %s czn+0x%zx cv=%s cLen=%u gLen=%u gznSize=%zu gOffCandidates=%zu "
                         "firstGOff=0x%zx flags@0x10=0x%02X chan@d0x10=%u idx@d0x10=%u",
                         container.c_str(), stem.c_str(), pos, hex32(cv).c_str(), cLength, gLength,
                         gb.size(), it->second.size(), it->second[0], cb[pos + 0x10], c2, i2);
                g_passNoDExamples.push_back(buf);
            }
        } else {
            if (pass) ++g_dHistPass[validD[0]]; else ++g_dHistFail[validD[0]];
            ++row.dHist[validD[0]];
            if (validD.size() > 1) { if (pass) ++g_ambiguousPass; else ++g_ambiguousFail; }
        }

        if (!pass && row.firstAnchorDetail.empty()) {
            char buf[512];
            snprintf(buf, sizeof(buf),
                     "anchor czn+0x%zx cv=%s cLen=%u gLen=%u gznSize=%zu gOff=0x%zx d=%s "
                     "flags@+0x10=0x%02X flags@+0x14=0x%02X channelCount=%u indexCount=%u "
                     "word@+0x10=%s",
                     pos, hex32(cv).c_str(), cLength, gLength, gb.size(), chosenGOff,
                     validD.empty() ? "none" : hex32((uint32_t)validD[0]).c_str(),
                     pos + 0x10 < cb.size() ? cb[pos + 0x10] : 0xFF,
                     pos + 0x14 < cb.size() ? cb[pos + 0x14] : 0xFF, cc, ic,
                     pos + 0x14 <= cb.size() ? hex32(u32at(cb.data(), pos + 0x10)).c_str() : "n/a");
            row.firstAnchorDetail = buf;
        }
        if (!pass && g_examples.size() < 40) {
            char buf[640];
            snprintf(buf, sizeof(buf), "%s :: %s  czn+0x%zx  d=%s  cLen=%u gLen=%u chan=%u idx=%u", container.c_str(),
                     stem.c_str(), pos, validD.empty() ? "none" : hex32((uint32_t)validD[0]).c_str(), cLength,
                     gLength, cc, ic);
            g_examples.push_back(buf);
        }
    }

    if (!pass) {
        if (row.anchors == 0) ++g_failFilesWithNoAnchorAtAll;
        else if (row.noD == 0 && row.dHist.size() == 1 && row.dHist.count(0x14))
            ++g_failFilesFullyExplainedBy0x14;
        g_failRows.push_back(std::move(row));
    }
}

void walk(const vpp::Container& c, const std::string& path) {
    std::map<std::string, size_t> czn, gzn;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        std::string stem;
        if (stemFor(n, ".czn_pc", stem)) czn[stem] = i;
        else if (stemFor(n, ".gzn_pc", stem)) gzn[stem] = i;
    }
    for (const auto& kv : czn) {
        try {
            auto g = gzn.find(kv.first);
            if (g == gzn.end()) continue;
            std::vector<uint8_t> cb, gb;
            if (!getEntry(c, kv.second, cb)) continue;
            if (!getEntry(c, g->second, gb)) continue;
            if (gb.empty() || cb.empty()) continue;
            process(path, kv.first, cb, gb);
        } catch (const std::exception&) { continue; }
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walk(c.openNested(i), path + " > " + c.entries()[i].name); } catch (const std::exception&) {}
        }
    }
}

void printReport() {
    printf("\n=== PAIRS ===\n");
    printf("non-empty pairs            : %lld\n", g_pairs);
    printf("  locate() finds >=1 block : %lld\n", g_pairsPass);
    printf("  locate() finds 0 blocks  : %lld\n", g_pairsFail);
    printf("  total blocks returned by locate() across all pairs : %lld\n", g_blocksFromLocate);

    printf("\n=== ANCHORS (4-aligned u32==9 whose next u32 is a 16-aligned value in the paired .gzn_pc) ===\n");
    printf("total anchors : %lld   (in passing files %lld, in failing files %lld)\n", g_anchors,
           g_anchorsPass, g_anchorsFail);
    printf("cLength bookend self-check (u32 at pos+cLength-4 == check value): pass %lld/%lld, fail %lld/%lld\n",
           g_cLenBookendOkPass, g_anchorsPass, g_cLenBookendOkFail, g_anchorsFail);

    printf("\n=== HEADER DISPLACEMENT d THAT MAKES THE FULL G-SEGMENT WALK CONSISTENT ===\n");
    printf("(kHeaderStart in the shipped reader is 0x10)\n");
    printf("PASSING files:\n");
    for (const auto& kv : g_dHistPass) printf("  d=0x%02zX : %lld anchors\n", kv.first, kv.second);
    printf("  no d validated : %lld anchors\n", g_noDPass);
    printf("  more than one d validated (ambiguous) : %lld anchors\n", g_ambiguousPass);
    printf("FAILING files:\n");
    for (const auto& kv : g_dHistFail) printf("  d=0x%02zX : %lld anchors\n", kv.first, kv.second);
    printf("  no d validated : %lld anchors\n", g_noDFail);
    printf("  more than one d validated (ambiguous) : %lld anchors\n", g_ambiguousFail);

    printf("\n=== INDEPENDENT ORACLE: channel record's own declared data offset (record+0x10) ===\n");
    printf("    src/mesh_block.cpp never reads this field, so it is untouched by the walk\n");
    printf("PASSING files:\n");
    for (const auto& kv : g_agreeHistPass) printf("  d=0x%02zX : %lld anchors\n", kv.first, kv.second);
    printf("  no d agrees : %lld    ambiguous (>1 d agrees) : %lld\n", g_agreeNonePass, g_agreeAmbigPass);
    printf("FAILING files:\n");
    for (const auto& kv : g_agreeHistFail) printf("  d=0x%02zX : %lld anchors\n", kv.first, kv.second);
    printf("  no d agrees : %lld    ambiguous (>1 d agrees) : %lld\n", g_agreeNoneFail, g_agreeAmbigFail);
    printf("\n=== THIRD VARIANT: walk using each channel record's DECLARED offset (record+0x10) ===\n");
    printf("PASSING files:\n");
    for (const auto& kv : g_declHistPass) printf("  d=0x%02zX : %lld anchors\n", kv.first, kv.second);
    printf("  none : %lld\n", g_declNonePass);
    printf("FAILING files:\n");
    for (const auto& kv : g_declHistFail) printf("  d=0x%02zX : %lld anchors\n", kv.first, kv.second);
    printf("  none : %lld\n", g_declNoneFail);
    printf("--- cross-tab: strict-walk d vs declared-offset-WALK d ---\n");
    for (const auto& kv : g_strictVsDeclWalk) printf("  %-60s %lld\n", kv.first.c_str(), kv.second);

    printf("\n--- cross-tab: strict-walk d vs declared-offset-oracle d (all anchors) ---\n");
    for (const auto& kv : g_strictVsAgree) printf("  %-60s %lld\n", kv.first.c_str(), kv.second);

    printf("\n=== SECONDARY, LOOSER CONTRACT (g-length covers content only, no trailing bookend) ===\n");
    printf("    consulted ONLY for anchors the strict contract rejected\n");
    printf("PASSING files (%lld strict-rejected anchors):\n", g_noDPass);
    for (const auto& kv : g_dHistRelaxedPass) printf("  d=0x%02zX : %lld\n", kv.first, kv.second);
    printf("  still nothing : %lld\n", g_relaxedNonePass);
    printf("FAILING files (%lld strict-rejected anchors):\n", g_noDFail);
    for (const auto& kv : g_dHistRelaxedFail) printf("  d=0x%02zX : %lld\n", kv.first, kv.second);
    printf("  still nothing : %lld\n", g_relaxedNoneFail);

    printf("\n=== production flags-byte peek (at pos+0x10) vs the d that actually works ===\n");
    for (const auto& kv : g_flagsVsD) printf("  %-52s %lld\n", kv.first.c_str(), kv.second);

    printf("\n=== FAILING FILES, per file ===\n");
    printf("failing files with 0 anchors at all                 : %lld\n", g_failFilesWithNoAnchorAtAll);
    printf("failing files where EVERY anchor validates at d=0x14: %lld\n", g_failFilesFullyExplainedBy0x14);
    int i = 0;
    for (const auto& r : g_failRows) {
        ++i;
        std::string h;
        for (const auto& kv : r.dHist) h += " d=0x" + std::to_string(kv.first) + ":" + std::to_string(kv.second);
        printf("%3d  czn=%-9zu gzn=%-9zu anchors=%-3lld noD=%-3lld%s  %s :: %s\n", i, r.cznSize, r.gznSize,
               r.anchors, r.noD, h.c_str(), r.container.c_str(), r.stem.c_str());
        if (!r.firstAnchorDetail.empty()) printf("       %s\n", r.firstAnchorDetail.c_str());
    }

    printf("\n=== EXAMPLES (failing-file anchors, first %zu) ===\n", g_examples.size());
    for (const auto& e : g_examples) printf("  %s\n", e.c_str());

    printf("\n=== EXAMPLES (PASSING-file anchors that validate at NO d - replay-fidelity check) ===\n");
    for (const auto& e : g_passNoDExamples) printf("  %s\n", e.c_str());
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> b = readFile(argv[i]);
        if (b.empty()) { printf("skip: %s\n", argv[i]); continue; }
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walk(c, argv[i]);
            printf("scanned %s\n", argv[i]);
            fflush(stdout);
        } catch (const std::exception& ex) { printf("FAILED to open %s: %s\n", argv[i], ex.what()); }
    }
    printReport();
    return 0;
}
