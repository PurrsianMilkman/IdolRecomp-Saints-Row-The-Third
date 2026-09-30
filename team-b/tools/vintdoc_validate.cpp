// Population validator for sr3vintdoc (spec-vint-doc-format.md), run on the
// owner's PC through the bridge (team-b/bridge-jobs/03_vintdoc_validate.json).
//
// usage: vintdoc_validate <out_dir> <archive.vpp_pc> [more archives...] [--dump <name-substring>]
//
// Part 1 - CONFIRMED checks. Every .vint_doc / .vint_xdoc entry in each
// archive (nested .str2_pc containers included, every archive reported on
// its own, never first-found-only) is parsed with the sr3vintdoc library
// and the spec's own population figures (Sec1.1/Sec2/Sec3.1, measured on
// interface_startup.vpp_pc's 159 files) are recomputed and printed beside
// the spec's number. Exclusions (entries that failed to decompress, text/XML
// documents, header or string-table parse failures) are counted and printed,
// never skipped silently.
//
// Part 2 - HYPOTHESIS test, not used by the library. The spec leaves three
// facts a full-document walk needs OPEN or ambiguous (vint_doc.h, top note).
// Each combination of candidate answers is scored on every file:
//   crit-resource start: A = absolute header +0x16, B = right after the
//                        string-offset array (the string-pool base)
//   property list:       P1 = baseline offset is absolute, main cursor
//                        resumes after the override header; P2 = baseline
//                        offset relative to the property block's own start,
//                        same resume; P3 = list inline right after the
//                        override header, offsets ignored
//   record byte order:   O1 = tag, hash, value; O2 = hash, tag, value
// The baseline list is always the one read (no active resolution is modelled
// - CHOSEN). A combination "walks" a file when every count-driven record and
// every property list decodes with known tags only; it "lands" when the
// furthest byte any record consumed is exactly end-of-file. Corroborators
// that can fail independently of landing: the share of element type indices
// resolving to one of the 13 registered type names, and the share of
// metadata indices inside the string table. The number of combinations that
// land per file is printed as the ambiguity count.
//
// Outputs in <out_dir>: vintdoc_summary.txt, vintdoc_per_file.tsv (names,
// sizes and numbers only - no string content), and with --dump a
// vintdoc_dump_<name>.json holding one document's header and resolved
// string table. The dump contains game text: it goes to the private bus
// only and must never be committed to the project repository.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "sr3vintdoc/vint_doc.h"
#include "vpp/container.h"

namespace fs = std::filesystem;
using namespace sr3vintdoc;

namespace {

struct Doc {
    std::string archive;
    std::string chain;
    std::vector<uint8_t> bytes;
};

struct ArchiveTally {
    std::string archive;
    long entriesSeen = 0;
    long vintDocEntries = 0;
    long vintXdocEntries = 0;
    long decodeFailures = 0;
    std::vector<std::string> decodeFailureNames;
    bool openFailed = false;
    std::string openError;
};

bool endsWithCI(const std::string& s, const std::string& suf) {
    if (s.size() < suf.size()) return false;
    for (size_t i = 0; i < suf.size(); ++i) {
        char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[s.size() - suf.size() + i])));
        if (a != suf[i]) return false;
    }
    return true;
}

void walk(const vpp::Container& c, const std::string& archive, const std::string& chain, ArchiveTally& t,
          std::vector<Doc>& docs) {
    const auto& entries = c.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        ++t.entriesSeen;
        std::string name = e.name.empty() ? ("<unnamed hash=" + std::to_string(e.nameHash) + ">") : e.name;
        std::string childChain = chain.empty() ? name : (chain + " > " + name);
        bool isDoc = endsWithCI(name, ".vint_doc");
        bool isXdoc = endsWithCI(name, ".vint_xdoc");
        if (isDoc) ++t.vintDocEntries;
        if (isXdoc) ++t.vintXdocEntries;
        try { // per-entry guard: one bad entry must not drop its siblings (HANDOFF rule 6)
            if (e.payload.kind == vpp::PayloadKind::Raw) {
                if (isDoc || isXdoc) {
                    vpp::ByteView raw = c.rawEntryBytes(i);
                    docs.push_back({archive, childChain, std::vector<uint8_t>(raw.data(), raw.data() + raw.size())});
                    continue;
                }
                try {
                    vpp::Container child = c.openNested(i);
                    walk(child, archive, childChain, t, docs);
                } catch (const std::exception&) {
                    // not a nested container
                }
            } else {
                if (!(isDoc || isXdoc) && !endsWithCI(name, ".str2_pc")) continue;
                vpp::DecompressResult r = c.decompressEntry(i);
                if (r.status != vpp::DecodeStatus::Ok) {
                    if (isDoc || isXdoc) {
                        ++t.decodeFailures;
                        t.decodeFailureNames.push_back(childChain);
                    }
                    continue;
                }
                if (isDoc || isXdoc) {
                    docs.push_back({archive, childChain, std::move(r.data)});
                } else {
                    vpp::Container child(vpp::ByteView(r.data.data(), r.data.size()));
                    walk(child, archive, childChain, t, docs);
                }
            }
        } catch (const std::exception&) {
            if (isDoc || isXdoc) {
                ++t.decodeFailures;
                t.decodeFailureNames.push_back(childChain);
            }
        }
    }
}

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n > 0 ? n : 0));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}

// ---- Part 2: hypothesis walk ---------------------------------------------

struct Combo {
    char crit;  // 'A' or 'B'
    int prop;   // 1, 2, 3
    int order;  // 1, 2
    std::string label() const {
        return std::string(1, crit) + "-P" + std::to_string(prop) + "-O" + std::to_string(order);
    }
};

struct WalkResult {
    bool walked = false;
    bool landed = false;
    std::string failure;
    long elements = 0;
    long properties = 0;
    long typeResolvedRegistered = 0;
    long metadataIndices = 0;
    long metadataIndicesInTable = 0;
};

struct HypoWalker {
    vpp::ByteView bytes;
    const Header& h;
    const StringTable& st;
    Combo combo;
    size_t furthest = 0;
    WalkResult r;

    void note(size_t pos) { furthest = std::max(furthest, pos); }

    void readList(Cursor& lc) {
        for (int guard = 0; guard < 100000; ++guard) {
            uint8_t tag;
            if (combo.order == 1) {
                tag = lc.u8();
                if (tag == 0) { note(lc.pos()); return; }
                lc.u32();
            } else {
                lc.u32();
                tag = lc.u8();
                if (tag == 0) { note(lc.pos()); return; }
            }
            int size = propertyValueSize(tag);
            if (size < 0) throw FormatError("unknown tag " + std::to_string(tag));
            lc.skip(static_cast<size_t>(size));
            ++r.properties;
        }
        throw FormatError("property list guard");
    }

    void element(Cursor& c, int depth) {
        if (depth > 64) throw FormatError("depth limit");
        if (++r.elements > 200000) throw FormatError("element limit");
        size_t blockStart;
        ElementHead e = readElementHead(c);
        std::string type;
        if (resolveStringNulTerminated(bytes, st, e.typeIndex, type) && isRegisteredElementType(type))
            ++r.typeResolvedRegistered;
        blockStart = c.pos();
        PropertyBlockHeader ph = readPropertyBlockHeader(c);
        note(c.pos());
        if (combo.prop == 3) {
            readList(c);
        } else {
            uint64_t at = combo.prop == 1 ? ph.baselineOffsetRaw
                                          : static_cast<uint64_t>(blockStart) + ph.baselineOffsetRaw;
            if (at >= bytes.size()) throw FormatError("list offset outside file");
            Cursor lc(bytes, static_cast<size_t>(at));
            readList(lc);
        }
        note(c.pos());
        for (uint16_t i = 0; i < e.childCount; ++i) element(c, depth + 1);
    }

    WalkResult run() {
        try {
            size_t critStart = combo.crit == 'A' ? h.secondaryOffsetRaw : st.base;
            Cursor c(bytes, critStart);
            for (uint32_t i = 0; i < h.criticalResourceCount; ++i) readCriticalResource(c, h.version);
            for (uint32_t i = 0; i < h.metadataCount; ++i) {
                MetadataEntry m = readMetadataEntry(c);
                r.metadataIndices += 2;
                r.metadataIndicesInTable += (m.nameIndex < st.offsets.size()) + (m.valueIndex < st.offsets.size());
            }
            note(c.pos());
            for (uint32_t i = 0; i < static_cast<uint32_t>(h.elementCount) + h.animationCount; ++i) element(c, 0);
            note(c.pos());
            r.walked = true;
            r.landed = furthest == bytes.size();
        } catch (const std::exception& ex) {
            r.failure = ex.what();
        }
        return r;
    }
};

bool printable(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char ch : s)
        if (ch < 0x20 || ch > 0x7E) return false;
    return true;
}

std::string jsonEscape(const std::string& s) {
    std::string o;
    for (unsigned char ch : s) {
        if (ch == '"' || ch == '\\') { o += '\\'; o += static_cast<char>(ch); }
        else if (ch < 0x20 || ch > 0x7E) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", ch); o += buf; }
        else o += static_cast<char>(ch);
    }
    return o;
}

template <typename T>
std::string rangeMedian(std::vector<T> v) {
    if (v.empty()) return "n/a";
    std::sort(v.begin(), v.end());
    std::ostringstream o;
    o << v.front() << "-" << v.back() << ", median " << v[v.size() / 2];
    return o.str();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: vintdoc_validate <out_dir> <archive.vpp_pc> [more archives...] [--dump <name-substring>]\n";
        return 1;
    }
    fs::path outDir = argv[1];
    fs::create_directories(outDir);
    std::vector<std::string> archives;
    std::string dumpName;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--dump" && i + 1 < argc) dumpName = argv[++i];
        else archives.push_back(a);
    }

    std::vector<Combo> combos;
    for (char cr : {'A', 'B'})
        for (int p : {1, 2, 3})
            for (int o : {1, 2}) combos.push_back({cr, p, o});

    std::ofstream summary(outDir / "vintdoc_summary.txt");
    auto line = [&](const std::string& s) { summary << s << "\n"; std::cout << s << "\n"; };
    std::ofstream tsv(outDir / "vintdoc_per_file.tsv");
    tsv << "archive\tentry\tsize\tmagic_ok\tversion\treserved04\tfield0A\tmeta\tcrit\tsec16\tsec16_lt_size\telems\tanims"
           "\tstr_count\tstr_resolved\tstr_clean\tstr_clean_lt_90pct";
    for (auto& c : combos) tsv << "\t" << c.label();
    tsv << "\tcombos_landed\n";

    int exitCode = 0;
    for (const std::string& archivePath : archives) {
        ArchiveTally t;
        t.archive = fs::path(archivePath).filename().string();
        std::vector<Doc> docs;
        std::vector<uint8_t> archiveBytes = readFile(archivePath);
        if (archiveBytes.empty()) {
            t.openFailed = true;
            t.openError = "could not read file";
        } else {
            try {
                vpp::Container root(vpp::ByteView(archiveBytes.data(), archiveBytes.size()));
                walk(root, t.archive, "", t, docs);
            } catch (const std::exception& ex) {
                t.openFailed = true;
                t.openError = ex.what();
            }
        }

        line("\n=== " + t.archive + " ===");
        if (t.openFailed) {
            line("ARCHIVE OPEN FAILED: " + t.openError);
            exitCode = 2;
            continue;
        }
        line("entries walked (nested included): " + std::to_string(t.entriesSeen));
        line(".vint_doc entries: " + std::to_string(t.vintDocEntries) + "   .vint_xdoc entries: " +
             std::to_string(t.vintXdocEntries) + "   (spec Sec1.1: 159 / 0 in interface_startup.vpp_pc)");
        line("excluded, failed to decompress/read: " + std::to_string(t.decodeFailures));
        for (auto& n : t.decodeFailureNames) line("  decode failure: " + n);

        long parsed = 0, notMagic = 0, headerFail = 0, tableFail = 0;
        long magicOk = 0, reservedZero = 0, v1 = 0, v2 = 0, vOther = 0, f0aZero = 0, sec16Lt = 0, cleanLt90 = 0;
        std::map<uint32_t, int> f0aValues;
        std::vector<uint32_t> metas, crits;
        std::vector<uint16_t> elems, anims;
        std::map<std::string, long> comboWalked, comboLanded, comboType, comboElems, comboMetaIn, comboMeta;
        std::map<int, long> landedHistogram;

        for (const Doc& d : docs) {
            vpp::ByteView v(d.bytes.data(), d.bytes.size());
            tsv << d.archive << "\t" << d.chain << "\t" << d.bytes.size();
            if (!looksLikeVintDoc(v)) {
                ++notMagic;
                tsv << "\t0\n";
                continue;
            }
            Header h;
            try {
                h = parseHeader(v);
            } catch (const std::exception&) {
                ++headerFail;
                tsv << "\theader_fail\n";
                continue;
            }
            ++magicOk;
            reservedZero += h.reserved04 == 0;
            if (h.version == 1) ++v1; else if (h.version == 2) ++v2; else ++vOther;
            if (h.field0ARaw == 0) ++f0aZero; else ++f0aValues[h.field0ARaw];
            sec16Lt += h.secondaryOffsetRaw < d.bytes.size();
            metas.push_back(h.metadataCount);
            crits.push_back(h.criticalResourceCount);
            elems.push_back(h.elementCount);
            anims.push_back(h.animationCount);
            char f0a[16];
            std::snprintf(f0a, sizeof f0a, "0x%08X", h.field0ARaw);
            tsv << "\t1\t" << h.version << "\t" << h.reserved04 << "\t" << f0a << "\t" << h.metadataCount << "\t"
                << h.criticalResourceCount << "\t" << h.secondaryOffsetRaw << "\t"
                << (h.secondaryOffsetRaw < d.bytes.size()) << "\t" << h.elementCount << "\t" << h.animationCount;

            StringTable st;
            try {
                st = parseStringTable(v);
            } catch (const std::exception&) {
                ++tableFail;
                tsv << "\ttable_fail\n";
                continue;
            }
            ++parsed;
            long resolved = 0, clean = 0;
            std::string s;
            for (uint32_t i = 0; i < st.offsets.size(); ++i) {
                if (resolveStringNulTerminated(v, st, i, s)) {
                    ++resolved;
                    clean += printable(s);
                }
            }
            bool lt90 = !st.offsets.empty() && clean * 10 < static_cast<long>(st.offsets.size()) * 9;
            cleanLt90 += lt90;
            tsv << "\t" << st.offsets.size() << "\t" << resolved << "\t" << clean << "\t" << lt90;

            int landedHere = 0;
            for (auto& combo : combos) {
                HypoWalker w{v, h, st, combo};
                WalkResult r = w.run();
                std::string k = combo.label();
                comboWalked[k] += r.walked;
                comboLanded[k] += r.landed;
                if (r.walked) {
                    comboType[k] += r.typeResolvedRegistered;
                    comboElems[k] += r.elements;
                    comboMetaIn[k] += r.metadataIndicesInTable;
                    comboMeta[k] += r.metadataIndices;
                }
                landedHere += r.landed;
                tsv << "\t" << (r.landed ? "LAND" : r.walked ? "walk" : "fail");
            }
            ++landedHistogram[landedHere];
            tsv << "\t" << landedHere << "\n";

            if (!dumpName.empty() && d.chain.find(dumpName) != std::string::npos) {
                std::string safe = fs::path(d.chain).filename().string();
                for (char& ch : safe)
                    if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '.' && ch != '_') ch = '_';
                std::ofstream j(outDir / ("vintdoc_dump_" + safe + ".json"));
                j << "{\n  \"entry\": \"" << jsonEscape(d.chain) << "\",\n  \"size\": " << d.bytes.size()
                  << ",\n  \"header\": {\"version\": " << h.version << ", \"reserved04\": " << h.reserved04
                  << ", \"field0A_raw\": " << h.field0ARaw << ", \"metadata_count\": " << h.metadataCount
                  << ", \"critical_resource_count\": " << h.criticalResourceCount
                  << ", \"secondary_offset_raw\": " << h.secondaryOffsetRaw << ", \"element_count\": "
                  << h.elementCount << ", \"animation_count\": " << h.animationCount << "},\n"
                  << "  \"string_pool_base\": " << st.base << ",\n  \"strings\": [\n";
                for (uint32_t i = 0; i < st.offsets.size(); ++i) {
                    bool ok = resolveStringNulTerminated(v, st, i, s);
                    j << "    {\"index\": " << i << ", \"offset\": " << st.offsets[i] << ", \"resolved\": "
                      << (ok ? "true" : "false") << ", \"text\": \"" << jsonEscape(s) << "\"}"
                      << (i + 1 < st.offsets.size() ? "," : "") << "\n";
                }
                j << "  ]\n}\n";
            }
        }

        long n = magicOk;
        auto frac = [](long a, long b) { return std::to_string(a) + "/" + std::to_string(b); };
        line("\n--- Part 1: CONFIRMED checks (spec figure is for interface_startup.vpp_pc, 159 files) ---");
        line("excluded, not binary (no 0x3027 magic - text/XML or other): " + std::to_string(notMagic));
        line("excluded, header parse failed: " + std::to_string(headerFail) +
             "   string table parse failed: " + std::to_string(tableFail));
        line("magic 0x00003027: " + frac(magicOk, magicOk + notMagic + headerFail) +
             " (of every .vint_doc/.vint_xdoc entry read)   spec: 159/159");
        line("reserved +0x04 == 0: " + frac(reservedZero, n) + "   spec: 159/159");
        line("version 1 / 2 / other: " + std::to_string(v1) + " / " + std::to_string(v2) + " / " +
             std::to_string(vOther) + "   spec: 4 / 155 / 0");
        {
            std::string vals;
            for (auto& kv : f0aValues) {
                char b[32];
                std::snprintf(b, sizeof b, " 0x%08X x%d", kv.first, kv.second);
                vals += b;
            }
            line("+0x0A == 0: " + frac(f0aZero, n) + "   spec: 154/159 (3 share 0x3EB0C000); non-zero:" + vals);
        }
        line("+0x16 < file size: " + frac(sec16Lt, n) + "   spec: 159/159");
        line("metadata count: " + rangeMedian(metas) + "   spec: 0-8, median 2");
        line("critical-resource count: " + rangeMedian(crits) + "   spec: 0-8, median 1");
        line("top-level elements: " + rangeMedian(elems) + "   spec: 0-18, median 1");
        line("top-level animations: " + rangeMedian(anims) + "   spec: 0-58, median 1");
        line("files with < 90% of string entries resolving to clean printable text: " + frac(cleanLt90, parsed) +
             "   spec: 85/159 (this tool's criterion: NUL found, non-empty, all bytes 0x20-0x7E)");

        line("\n--- Part 2: HYPOTHESIS layout grid (not used by the library; see this tool's top comment) ---");
        line("combo      walked      landed      element types -> registered name   metadata indices in table");
        for (auto& combo : combos) {
            std::string k = combo.label();
            char buf[256];
            std::snprintf(buf, sizeof buf, "%-9s  %-10s  %-10s  %-33s  %s", k.c_str(),
                          frac(comboWalked[k], parsed).c_str(), frac(comboLanded[k], parsed).c_str(),
                          frac(comboType[k], comboElems[k]).c_str(), frac(comboMetaIn[k], comboMeta[k]).c_str());
            line(buf);
        }
        std::string hist;
        for (auto& kv : landedHistogram) hist += " " + std::to_string(kv.first) + ":" + std::to_string(kv.second);
        line("ambiguity - number of combos landing per file (combos:files):" + hist);
    }
    return exitCode;
}
