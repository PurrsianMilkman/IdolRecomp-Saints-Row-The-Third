// Ad hoc real-data validation (not a deliverable): runs the real
// sr3conversation and sr3cutscene parsers over every .ctdg_pc / .csc_pc in
// the given archives and reproduces each spec's published population
// statistics beside the spec's own figures.
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3conversation/conversation.h"
#include "sr3cutscene/camera_script.h"
#include "vpp/container.h"

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    std::streamsize size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    if (size > 0) f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

bool endsWith(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

// ---- .ctdg_pc tallies ----
int t_files = 0, t_ok = 0, t_fail = 0, t_truncName = 0;
size_t t_minSize = SIZE_MAX, t_maxSize = 0;
long long t_records = 0;
std::set<uint32_t> t_speakers, t_lines;
std::map<size_t, int> t_recordCounts, t_speakerCounts;
long long t_f8_zero = 0, t_f8_nonzero = 0;
int t_f8_mult100 = 0;
int t_f8_min = 2147483647, t_f8_max = -2147483647;
int t_lineDistinctWithinFile = 0;

// ---- .csc_pc tallies ----
int c_files = 0, c_ok = 0, c_fail = 0, c_gsc = 0;
size_t c_minSize = SIZE_MAX, c_maxSize = 0;
long long c_shots = 0, c_channelPtrs = 0;
int c_minShots = 1 << 30, c_maxShots = 0;
long long c_contigPairs = 0, c_contigOk = 0;
int c_firstStartsZero = 0;
long long c_ch0Keys = 0;
std::map<size_t, long long> c_presentByChannel;      // channel -> records with keyCount>0
std::map<size_t, long long> c_keysByChannel;
int c_optionalAbsent = 0, c_optionalSingle = 0, c_optionalMulti = 0;
long long c_ascendingTracks = 0, c_multiKeyTracks = 0;
int c_fovInRange = 0, c_fovTotal = 0;
int c_nullPtrs = 0;

void checkCtdg(const std::string& name, vpp::ByteView bytes) {
    ++t_files;
    if (bytes.size() < t_minSize) t_minSize = bytes.size();
    if (bytes.size() > t_maxSize) t_maxSize = bytes.size();
    try {
        sr3conversation::Conversation c =
            sr3conversation::Conversation::parse(sr3conversation::ByteView(bytes.data(), bytes.size()));
        ++t_ok;
        if (c.sourceNameTruncated()) ++t_truncName;
        t_records += static_cast<long long>(c.turns().size());
        ++t_recordCounts[c.turns().size()];
        ++t_speakerCounts[c.distinctSpeakerIds().size()];
        std::set<uint32_t> linesHere;
        for (const auto& t : c.turns()) {
            t_speakers.insert(t.speakerId);
            t_lines.insert(t.lineId);
            linesHere.insert(t.lineId);
            if (t.field_0x08_raw == 0) {
                ++t_f8_zero;
            } else {
                ++t_f8_nonzero;
                int v = t.field_0x08_asInt32();
                if (v % 100 == 0) ++t_f8_mult100;
                if (v < t_f8_min) t_f8_min = v;
                if (v > t_f8_max) t_f8_max = v;
            }
        }
        if (linesHere.size() == c.turns().size()) ++t_lineDistinctWithinFile;
    } catch (const std::exception& ex) {
        ++t_fail;
        printf("CTDG FAIL %s (%zu bytes): %s\n", name.c_str(), bytes.size(), ex.what());
    }
}

void checkCsc(const std::string& name, vpp::ByteView bytes) {
    ++c_files;
    if (bytes.size() < c_minSize) c_minSize = bytes.size();
    if (bytes.size() > c_maxSize) c_maxSize = bytes.size();
    try {
        sr3cutscene::CameraScript s =
            sr3cutscene::CameraScript::parse(sr3cutscene::ByteView(bytes.data(), bytes.size()));
        ++c_ok;
        int shots = static_cast<int>(s.shots().size());
        c_shots += shots;
        if (shots < c_minShots) c_minShots = shots;
        if (shots > c_maxShots) c_maxShots = shots;
        if (!s.shots().empty() && s.shots()[0].startTime == 0.0f) ++c_firstStartsZero;

        for (size_t i = 0; i < s.shots().size(); ++i) {
            const auto& shot = s.shots()[i];
            if (i > 0) {
                ++c_contigPairs;
                if (shot.isContiguousWith(s.shots()[i - 1])) ++c_contigOk;
            }
            for (size_t ch = 0; ch < sr3cutscene::kChannelCount; ++ch) {
                const auto& channel = shot.channels[ch];
                c_channelPtrs += 2; // values + times pointer
                if (!channel.hasValues) ++c_nullPtrs;
                if (!channel.hasTimes) ++c_nullPtrs;
                if (channel.keyCount > 0) {
                    ++c_presentByChannel[ch];
                    c_keysByChannel[ch] += channel.keyCount;
                }
                if (channel.times.size() > 1) {
                    ++c_multiKeyTracks;
                    bool ascending = true;
                    for (size_t k = 1; k < channel.times.size(); ++k) {
                        if (!(channel.times[k] > channel.times[k - 1])) { ascending = false; break; }
                    }
                    if (ascending) ++c_ascendingTracks;
                }
                if (ch == sr3cutscene::kChannelFieldOfView && channel.keyCount > 0) {
                    for (float v : sr3cutscene::decodeFloats(channel)) {
                        ++c_fovTotal;
                        if (v > 0.0f && v < 3.14159265f) ++c_fovInRange;
                    }
                }
            }
            // The optional group: channels 3-7 move together.
            uint32_t k3 = shot.channels[3].keyCount;
            if (k3 == 0) ++c_optionalAbsent;
            else if (k3 == 1) ++c_optionalSingle;
            else ++c_optionalMulti;
            c_ch0Keys += shot.channels[sr3cutscene::kChannelPosition].keyCount;
        }
    } catch (const std::exception& ex) {
        ++c_fail;
        printf("CSC FAIL %s (%zu bytes): %s\n", name.c_str(), bytes.size(), ex.what());
    }
}

void walk(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const auto& e = c.entries()[i];
        if (endsWith(e.name, ".gsc_pc")) ++c_gsc;
        try {
            if (e.payload.kind == vpp::PayloadKind::Raw) {
                try {
                    vpp::Container nested = c.openNested(i);
                    walk(nested);
                    continue;
                } catch (const vpp::FormatError&) {
                }
                if (endsWith(e.name, ".ctdg_pc")) checkCtdg(e.name, c.rawEntryBytes(i));
                else if (endsWith(e.name, ".csc_pc")) checkCsc(e.name, c.rawEntryBytes(i));
            } else {
                auto r = c.decompressEntry(i);
                bool usable = r.status == vpp::DecodeStatus::Ok ||
                              r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
                              r.status == vpp::DecodeStatus::RecoveredSharedStream ||
                              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
                if (!usable) continue;
                if (endsWith(e.name, ".ctdg_pc")) checkCtdg(e.name, vpp::ByteView(r.data.data(), r.data.size()));
                else if (endsWith(e.name, ".csc_pc")) checkCsc(e.name, vpp::ByteView(r.data.data(), r.data.size()));
            }
        } catch (const std::exception&) {
            continue;
        }
    }
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> bytes = readFile(argv[i]);
        if (bytes.empty()) { printf("could not read %s\n", argv[i]); continue; }
        try {
            vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
            walk(c);
            printf("scanned %s\n", argv[i]);
            fflush(stdout);
        } catch (const std::exception& ex) {
            printf("skip %s: %s\n", argv[i], ex.what());
        }
    }

    printf("\n=== .ctdg_pc validation (spec-conversation-format.md) ===\n");
    printf("files found        : %d  (spec Sec2: 4,984 total across all archives)\n", t_files);
    printf("parsed OK          : %d\n", t_ok);
    printf("failed             : %d\n", t_fail);
    if (t_files) printf("size range         : %zu .. %zu  (spec Sec2: 56 .. 248)\n", t_minSize, t_maxSize);
    printf("total records      : %lld  (spec Sec2: 19,440 over the full 4,984)\n", t_records);
    printf("distinct speakers  : %zu  (spec Sec4: 57 over the full population)\n", t_speakers.size());
    printf("distinct line ids  : %zu  (spec Sec4: 4,153 over the full population)\n", t_lines.size());
    printf("truncated names    : %d  (spec Sec1: 1,187 over the full population)\n", t_truncName);
    printf("lines distinct within file: %d / %d  (spec Sec4: 4,983 of 4,984)\n", t_lineDistinctWithinFile, t_ok);
    printf("records per file   : (spec Sec2: 1-17; most common 2, then 3)\n");
    for (const auto& kv : t_recordCounts) printf("   %zu records: %d files\n", kv.first, kv.second);
    printf("distinct speakers per file: (spec Sec2: 2 in 4,149; 3 in 673; 1 in 114; 4 in 48)\n");
    for (const auto& kv : t_speakerCounts) printf("   %zu speakers: %d files\n", kv.first, kv.second);
    printf("field_0x08: zero in %lld, non-zero in %lld  (spec Sec4: zero in 19,109 of 19,440)\n",
           t_f8_zero, t_f8_nonzero);
    if (t_f8_nonzero) {
        printf("   non-zero: %d of %lld are multiples of 100, range %d .. %d  "
               "(spec Sec4: signed multiples of 100, roughly -500 .. +600)\n",
               t_f8_mult100, t_f8_nonzero, t_f8_min, t_f8_max);
    }

    printf("\n=== .csc_pc validation (spec-cutscene-camera-format.md) ===\n");
    printf("files found        : %d  (spec Sec2: 192 entries, 96 distinct)\n", c_files);
    printf("parsed OK          : %d\n", c_ok);
    printf("failed             : %d\n", c_fail);
    printf(".gsc_pc seen       : %d  (spec Sec2: 0)\n", c_gsc);
    if (c_files) printf("size range         : %zu .. %zu  (spec Sec2: 148 .. 45,240)\n", c_minSize, c_maxSize);
    printf("total shots        : %lld  (spec Sec2: 1,405 over 96 distinct files)\n", c_shots);
    if (c_ok) printf("shots per file     : %d .. %d  (spec Sec2: 1 .. 82)\n", c_minShots, c_maxShots);
    printf("first shot starts at 0.0 : %d / %d  (spec Sec4: 96/96)\n", c_firstStartsZero, c_ok);
    printf("contiguous consecutive pairs : %lld / %lld  (spec Sec4: 1,309/1,309)\n", c_contigOk, c_contigPairs);
    printf("null channel pointers : %d / %lld  (spec Sec3: 0 of 22,480)\n", c_nullPtrs, c_channelPtrs);
    printf("multi-key tracks strictly ascending : %lld / %lld  (spec Sec1: 1,999/1,999)\n",
           c_ascendingTracks, c_multiKeyTracks);
    printf("channel 0 keys     : %lld  (spec Sec5: 14,703)\n", c_ch0Keys);
    printf("FOV keys in (0, pi): %d / %d  (spec Sec5: all 2,345)\n", c_fovInRange, c_fovTotal);
    printf("per-channel presence (records with keys) / total keys:\n");
    for (size_t ch = 0; ch < sr3cutscene::kChannelCount; ++ch) {
        printf("   ch%zu: %lld records, %lld keys\n", ch, c_presentByChannel[ch], c_keysByChannel[ch]);
    }
    printf("optional group (ch3-7) per shot: absent %d, single-key %d, multi-key %d  "
           "(spec Sec5.2: 396 / 914 / 95)\n", c_optionalAbsent, c_optionalSingle, c_optionalMulti);

    return (t_fail == 0 && c_fail == 0) ? 0 : 1;
}
