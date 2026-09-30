// Throwaway investigation tool (per this task's own instructions) - NOT a
// shipped viewer command. Locates a real `.clmesh_pc`/`.glmesh_pc` pair
// inside `1018h0.str2_pc` (sr3_city_0.vpp_pc), parses it with the existing
// sr3clmesh::LevelMesh reader, and empirically tests where a material
// record's own 12-byte texture-binding entries resolve their `name_offset`
// against - the middle's own material-set name table (materialSetStep's
// nameTableOffset/nameTableLength, decoded in include/sr3clmesh/level_mesh.h)
// vs. the file-opening shared 0x00043854 MaterialBlock's own name table -
// plus whether MaterialRecord::hash0 matches the CRC-32 of any real
// .fxo_pc stem from shaders.vpp_pc (the same join sr3_viewer's `vehicle`
// command already uses, sr3fxo::crc32Raw).
//
// Usage: clmesh_probe <sr3_city_0.vpp_pc> <shaders.vpp_pc> [entryFilter]

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <map>
#include <string>
#include <vector>

#include "sr3clmesh/level_mesh.h"
#include "sr3fxo/wrapper_header.h"
#include "sr3geometry/material_block.h"
#include "sr3mesh/mesh_block.h"
#include "vpp/container.h"

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::vector<uint8_t> buf;
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == nullptr) return buf;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size > 0) {
        buf.resize(static_cast<size_t>(size));
        if (fread(buf.data(), 1, buf.size(), f) != buf.size()) buf.clear();
    }
    fclose(f);
    return buf;
}

bool endsWithNoCase(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        char a = s[s.size() - suffix.size() + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

bool entryBytes(const vpp::Container& container, size_t index, std::vector<uint8_t>& out) {
    const vpp::Entry& entry = container.entries()[index];
    if (entry.payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView raw = container.rawEntryBytes(index);
        out.assign(raw.data(), raw.data() + raw.size());
        return true;
    }
    vpp::DecompressResult r = container.decompressEntry(index);
    bool usable = r.status == vpp::DecodeStatus::Ok ||
                  r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
                  r.status == vpp::DecodeStatus::ContentValidated ||
                  r.status == vpp::DecodeStatus::RecoveredSharedStream ||
                  r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!usable) return false;
    out = std::move(r.data);
    return true;
}

bool findEntry(const vpp::Container& container, const std::string& name,
               std::vector<uint8_t>& out, std::vector<uint8_t>& siblingOut,
               const std::string& siblingName) {
    for (size_t i = 0; i < container.entries().size(); ++i) {
        if (container.entries()[i].name == name) {
            if (!entryBytes(container, i, out)) return false;
            for (size_t j = 0; j < container.entries().size(); ++j) {
                if (container.entries()[j].name == siblingName) {
                    entryBytes(container, j, siblingOut);
                    break;
                }
            }
            return true;
        }
    }
    for (size_t i = 0; i < container.entries().size(); ++i) {
        if (container.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        try {
            vpp::Container nested = container.openNested(i);
            if (findEntry(nested, name, out, siblingOut, siblingName)) return true;
        } catch (const std::exception&) {
        }
    }
    return false;
}

// Collects every (.clmesh_pc name -> its container-relative index) inside
// `container`, recursing into nested Raw containers, WITHOUT decompressing
// anything yet (cheap first pass to enumerate candidates).
void collectClmeshNames(const vpp::Container& container, std::vector<std::string>& names) {
    for (size_t i = 0; i < container.entries().size(); ++i) {
        const std::string& n = container.entries()[i].name;
        if (endsWithNoCase(n, ".clmesh_pc")) names.push_back(n);
        if (container.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                vpp::Container nested = container.openNested(i);
                collectClmeshNames(nested, names);
            } catch (const std::exception&) {
            }
        }
    }
}

void collectFxoStemsV(const vpp::Container& c, std::map<std::string, std::vector<std::string>>& stemToFiles) {
    static const std::vector<std::string> suffixes = {
        "_bms", "_bmc", "_bs", "_bc", "_ms", "_mc", "_mv", "_ts",
        "_fd", "_s", "_c", "_t", "_v",
    };
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        if (endsWithNoCase(n, ".fxo_pc")) {
            std::string noExt = n.substr(0, n.size() - 7);
            for (auto& ch : noExt) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
            std::string stem = noExt;
            for (const auto& suf : suffixes) {
                if (endsWithNoCase(noExt, suf)) { stem = noExt.substr(0, noExt.size() - suf.size()); break; }
            }
            stemToFiles[stem].push_back(n);
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { collectFxoStemsV(c.openNested(i), stemToFiles); } catch (const std::exception&) {}
        }
    }
}

// Reads a NUL-terminated string starting at `at`, or empty if `at` runs
// past `content` or is not NUL-terminated within a generous bound.
std::string readCString(vpp::ByteView content, size_t at, size_t maxLen = 128) {
    if (at >= content.size()) return "";
    std::string s;
    for (size_t i = 0; i < maxLen && at + i < content.size(); ++i) {
        uint8_t c = content.at(at + i);
        if (c == 0) return s;
        if (c < 0x20 || c > 0x7E) return ""; // not printable ASCII -> not a real string here
        s.push_back(static_cast<char>(c));
    }
    return ""; // ran off without a NUL inside maxLen - reject
}

// True if `at` is a "boundary" inside [tableStart, tableStart+tableLen):
// either the very first byte, or the byte right after a NUL.
bool isNameBoundary(vpp::ByteView content, size_t tableStart, size_t tableLen, size_t at) {
    if (at < tableStart || at >= tableStart + tableLen) return false;
    if (at == tableStart) return true;
    if (at - 1 < content.size() && content.at(at - 1) == 0) return true;
    return false;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: clmesh_probe <sr3_city_0.vpp_pc> <shaders.vpp_pc> [nameFilter]\n");
        return 1;
    }
    const std::string cityPath = argv[1];
    const std::string shadersPath = argv[2];
    const std::string filter = argc > 3 ? argv[3] : "";

    std::vector<uint8_t> cityArchive = readFile(cityPath);
    if (cityArchive.empty()) { std::fprintf(stderr, "could not read %s\n", cityPath.c_str()); return 1; }
    std::vector<uint8_t> shadersArchive = readFile(shadersPath);
    if (shadersArchive.empty()) { std::fprintf(stderr, "could not read %s\n", shadersPath.c_str()); return 1; }

    // ---- Step 0: real shaderHash -> real .fxo_pc stem table -------------
    std::map<std::string, std::vector<std::string>> stemToFiles;
    std::map<uint32_t, std::string> stemCrcToStem;
    try {
        vpp::Container shadersContainer(vpp::ByteView(shadersArchive.data(), shadersArchive.size()));
        collectFxoStemsV(shadersContainer, stemToFiles);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "shaders archive error: %s\n", ex.what());
        return 1;
    }
    for (const auto& kv : stemToFiles) {
        uint32_t crc = sr3fxo::crc32Raw(reinterpret_cast<const uint8_t*>(kv.first.data()), kv.first.size());
        stemCrcToStem[crc] = kv.first;
    }
    std::printf("[fxo] distinct real stems: %zu\n", stemToFiles.size());

    // ---- Step 1: find 1018h0.str2_pc, enumerate its .clmesh_pc entries --
    std::vector<uint8_t> str2Bytes, unusedSibling;
    vpp::Container cityContainer(vpp::ByteView(cityArchive.data(), cityArchive.size()));
    if (!findEntry(cityContainer, "1018h0.str2_pc", str2Bytes, unusedSibling, "")) {
        std::fprintf(stderr, "1018h0.str2_pc not found in %s\n", cityPath.c_str());
        return 1;
    }
    std::printf("[str2] 1018h0.str2_pc: %zu bytes\n", str2Bytes.size());

    vpp::Container str2Container(vpp::ByteView(str2Bytes.data(), str2Bytes.size()));
    std::vector<std::string> clmeshNames;
    collectClmeshNames(str2Container, clmeshNames);
    std::printf("[str2] %zu .clmesh_pc entries found\n", clmeshNames.size());

    struct Candidate {
        std::string name;
        uint32_t materialCount = 0;
        size_t clSize = 0, glSize = 0;
    };
    std::vector<Candidate> candidates;

    for (const std::string& name : clmeshNames) {
        if (!filter.empty() && name.find(filter) == std::string::npos) continue;
        std::string glName = name;
        size_t dot = glName.find_last_of('.');
        if (dot != std::string::npos) glName[dot + 1] = 'g';

        std::vector<uint8_t> clBytes, glBytes;
        if (!findEntry(str2Container, name, clBytes, glBytes, glName) || clBytes.empty()) continue;

        try {
            sr3clmesh::LevelMesh lm = sr3clmesh::LevelMesh::parse(
                vpp::ByteView(clBytes.data(), clBytes.size()));
            if (!lm.walkComplete()) continue;
            Candidate c;
            c.name = name;
            c.materialCount = lm.middle().materialCount;
            c.clSize = clBytes.size();
            c.glSize = glBytes.size();
            candidates.push_back(c);
        } catch (const std::exception&) {
            continue;
        }
    }

    std::printf("[clmesh] %zu candidates parsed cleanly (walkComplete)\n", candidates.size());
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.materialCount > b.materialCount; });
    size_t shown = 0;
    for (const auto& c : candidates) {
        if (shown++ >= 25) break;
        std::printf("  %-40s materials=%u cl=%zu gl=%zu\n", c.name.c_str(), c.materialCount, c.clSize, c.glSize);
    }

    if (candidates.empty()) {
        std::printf("[clmesh] no candidates - nothing more to test\n");
        return 0;
    }

    // ---- Step 2: pick the richest candidate (most materials) and test the
    // texture-binding hypothesis against real bytes. --------------------
    const Candidate& pick = candidates.front();
    std::printf("\n[pick] %s (materials=%u)\n", pick.name.c_str(), pick.materialCount);

    std::string glName = pick.name;
    { size_t dot = glName.find_last_of('.'); if (dot != std::string::npos) glName[dot + 1] = 'g'; }
    std::vector<uint8_t> clBytes, glBytes;
    findEntry(str2Container, pick.name, clBytes, glBytes, glName);
    vpp::ByteView clView(clBytes.data(), clBytes.size());

    sr3clmesh::LevelMesh lm = sr3clmesh::LevelMesh::parse(clView);
    const sr3clmesh::MiddleLayout& mid = lm.middle();
    std::printf("[middle] materialSetOffset=%zu materialCount=%u nameTableOffset=%zu nameTableLength=%zu\n",
                mid.materialSetOffset, mid.materialCount, mid.nameTableOffset, mid.nameTableLength);

    // Also parse the file-opening shared 0x00043854 MaterialBlock (lowercase
    // names) for the alternative hypothesis.
    sr3geometry::MaterialBlock outerMat;
    bool haveOuterMat = false;
    try {
        outerMat = sr3geometry::MaterialBlock::parse(sr3geometry::ByteView(clBytes.data(), clBytes.size()));
        haveOuterMat = true;
        std::printf("[outer material block] textureSlotCount=%u totalSize=%zu\n",
                    outerMat.textureSlotCount, outerMat.totalSize);
        for (size_t i = 0; i < outerMat.textureNames.size() && i < 20; ++i) {
            std::printf("    outerMat[%zu] = '%s'\n", i, outerMat.textureNames[i].c_str());
        }
    } catch (const std::exception& ex) {
        std::printf("[outer material block] parse failed: %s\n", ex.what());
    }

    // Direct, from-scratch byte scan on this (larger, 19-material) real
    // file too - same check as the lite_fixh deep test below, independent
    // confirmation on a second, much bigger sample.
    {
        int hitsLE = 0;
        for (size_t off = 0; off + 4 <= clBytes.size(); ++off) {
            if (clView.readU32LE(off) == 0x424BD00Du) ++hitsLE;
        }
        std::printf("[direct byte scan] literal 0x424BD00D occurrences in %s (%zu bytes): %d\n", pick.name.c_str(),
                    clBytes.size(), hitsLE);
    }

    // Print the middle's own local name table's raw strings, for inspection.
    if (mid.nameTableLength > 0) {
        std::printf("[middle name table] raw strings:\n");
        size_t at = mid.nameTableOffset;
        size_t end = mid.nameTableOffset + mid.nameTableLength;
        int shownNames = 0;
        while (at < end && shownNames < 40) {
            std::string s = readCString(clView, at, end - at + 1);
            if (!s.empty()) {
                std::printf("    [+%zu] '%s'\n", at - mid.nameTableOffset, s.c_str());
                at += s.size() + 1;
                ++shownNames;
            } else {
                ++at; // resync
            }
        }
    }

    std::vector<sr3clmesh::MaterialRecord> recs = lm.materialRecords(clView);
    std::printf("[materials] decoded %zu records\n", recs.size());

    int hypA_hit = 0, hypA_total = 0; // name_offset relative to middle().nameTableOffset
    int hypB_hit = 0, hypB_total = 0; // name_offset relative to outer MaterialBlock's name table start
    int hypC_hit = 0, hypC_total = 0; // name_offset absolute into content

    for (size_t mi = 0; mi < recs.size(); ++mi) {
        const auto& rec = recs[mi];
        std::printf("  material[%zu] offset=%zu hash0=0x%08X hash1=0x%08X flags=0x%02X texCount=%u constCount=%u vec4Count=%u\n",
                    mi, rec.offset, rec.hash0, rec.hash1, rec.flags, rec.textureBindingCount,
                    rec.constantNameCount, rec.vec4ConstantCount);

        auto it = stemCrcToStem.find(rec.hash0);
        if (it != stemCrcToStem.end()) {
            std::printf("      hash0 MATCHES real .fxo_pc stem CRC: '%s'\n", it->second.c_str());
        }
        auto it1 = stemCrcToStem.find(rec.hash1);
        if (it1 != stemCrcToStem.end()) {
            std::printf("      hash1 MATCHES real .fxo_pc stem CRC: '%s'\n", it1->second.c_str());
        }

        if (rec.textureBindingCount == 0) continue;
        const size_t runStart = rec.offset + sr3clmesh::kMatRecordHeaderSize; // header_start + 0x30
        for (uint16_t bi = 0; bi < rec.textureBindingCount; ++bi) {
            const size_t at = runStart + static_cast<size_t>(bi) * 12;
            if (at + 12 > clBytes.size()) { std::printf("      binding[%u] runs past EOF\n", bi); continue; }
            uint32_t nameOffset = clView.readU32LE(at + 0);
            uint32_t paramHash = clView.readU32LE(at + 4);
            uint32_t slotIdx = clView.readU32LE(at + 8);

            std::string hypAName, hypBName, hypCName;
            bool hypA_ok = false, hypB_ok = false, hypC_ok = false;
            if (mid.nameTableLength > 0) {
                size_t abs = mid.nameTableOffset + nameOffset;
                hypA_total++;
                if (isNameBoundary(clView, mid.nameTableOffset, mid.nameTableLength, abs)) {
                    hypAName = readCString(clView, abs);
                    if (!hypAName.empty()) { hypA_ok = true; hypA_hit++; }
                }
            }
            if (haveOuterMat) {
                size_t tableStart = 0x20; // MaterialBlock name table starts right after its 0x20 header
                size_t tableLen = outerMat.totalSize > tableStart ? outerMat.totalSize - tableStart : 0;
                size_t abs = tableStart + nameOffset;
                hypB_total++;
                if (isNameBoundary(clView, tableStart, tableLen, abs)) {
                    hypBName = readCString(clView, abs);
                    if (!hypBName.empty()) { hypB_ok = true; hypB_hit++; }
                }
            }
            {
                hypC_total++;
                if (nameOffset < clBytes.size()) {
                    // absolute: must also land on a boundary within the whole file (start or after a NUL)
                    bool boundary = (nameOffset == 0) || (nameOffset > 0 && clView.at(nameOffset - 1) == 0);
                    if (boundary) {
                        hypCName = readCString(clView, nameOffset);
                        if (!hypCName.empty()) { hypC_ok = true; hypC_hit++; }
                    }
                }
            }

            std::printf("      binding[%u] nameOffset=%u paramHash=0x%08X slot=%u | hypA(mid-local)=%s%s | hypB(outer-mat)=%s%s | hypC(absolute)=%s%s\n",
                        bi, nameOffset, paramHash, slotIdx,
                        hypA_ok ? "OK:" : "no", hypA_ok ? hypAName.c_str() : "",
                        hypB_ok ? "OK:" : "no", hypB_ok ? hypBName.c_str() : "",
                        hypC_ok ? "OK:" : "no", hypC_ok ? hypCName.c_str() : "");
        }
    }

    std::printf("\n[summary] hypA(middle-local table) %d/%d resolved\n", hypA_hit, hypA_total);
    std::printf("[summary] hypB(outer shared MaterialBlock table) %d/%d resolved\n", hypB_hit, hypB_total);
    std::printf("[summary] hypC(absolute file offset) %d/%d resolved\n", hypC_hit, hypC_total);

    // ---- Step 3: POPULATION-WIDE pass over EVERY real .clmesh_pc entry in
    // this str2_pc (not just the single richest pick above), same three
    // hypotheses plus the shaderHash join, aggregated. This is the number
    // that actually settles "does the mechanism generalise" rather than
    // "does it work on one lucky file".
    std::printf("\n===== POPULATION PASS: all %zu real .clmesh_pc entries in 1018h0.str2_pc =====\n",
                clmeshNames.size());
    int popHypA_hit = 0, popHypA_total = 0;
    int popHash0_hit = 0, popHash0_total = 0;
    int popFilesOk = 0, popFilesTotal = 0;
    for (const std::string& name : clmeshNames) {
        std::string gName = name;
        { size_t dot = gName.find_last_of('.'); if (dot != std::string::npos) gName[dot + 1] = 'g'; }
        std::vector<uint8_t> cb, gb;
        if (!findEntry(str2Container, name, cb, gb, gName) || cb.empty()) continue;
        popFilesTotal++;
        try {
            vpp::ByteView cv(cb.data(), cb.size());
            sr3clmesh::LevelMesh flm = sr3clmesh::LevelMesh::parse(cv);
            if (!flm.walkComplete()) {
                std::printf("  %-30s walk INCOMPLETE - skipped\n", name.c_str());
                continue;
            }
            const auto& fmid = flm.middle();
            std::vector<sr3clmesh::MaterialRecord> frecs = flm.materialRecords(cv);
            int fileHitA = 0, fileTotalA = 0, fileHash0Hit = 0, fileHash0Total = 0;
            for (const auto& rec : frecs) {
                fileHash0Total++;
                if (stemCrcToStem.find(rec.hash0) != stemCrcToStem.end()) { fileHash0Hit++; popHash0_hit++; }
                popHash0_total++;
                if (rec.textureBindingCount == 0) continue;
                const size_t runStart = rec.offset + sr3clmesh::kMatRecordHeaderSize;
                for (uint16_t bi = 0; bi < rec.textureBindingCount; ++bi) {
                    const size_t at = runStart + static_cast<size_t>(bi) * 12;
                    if (at + 12 > cb.size()) { fileTotalA++; popHypA_total++; continue; }
                    uint32_t nameOffset = cv.readU32LE(at + 0);
                    fileTotalA++; popHypA_total++;
                    if (fmid.nameTableLength == 0) continue;
                    size_t abs = fmid.nameTableOffset + nameOffset;
                    if (isNameBoundary(cv, fmid.nameTableOffset, fmid.nameTableLength, abs)) {
                        if (!readCString(cv, abs).empty()) { fileHitA++; popHypA_hit++; }
                    }
                }
            }
            popFilesOk++;
            std::printf("  %-30s materials=%2u  bindings %2d/%2d hypA-OK  shaderHash %2d/%2d resolved\n",
                        name.c_str(), fmid.materialCount, fileHitA, fileTotalA, fileHash0Hit, fileHash0Total);
        } catch (const std::exception& ex) {
            std::printf("  %-30s parse FAILED: %s\n", name.c_str(), ex.what());
        }
    }
    std::printf("\n[population summary] files parsed cleanly: %d/%d\n", popFilesOk, popFilesTotal);
    std::printf("[population summary] hypA (middle-local name table) texture bindings resolved: %d/%d\n",
                popHypA_hit, popHypA_total);
    std::printf("[population summary] hash0 -> real .fxo_pc stem CRC resolved: %d/%d\n",
                popHash0_hit, popHash0_total);

    // ---- Step 4: deep render-group parse test on the spec's own worked
    // example, `lite_fixh.clmesh_pc`/`.glmesh_pc` (spec-geometry-format.md
    // Sec4.2's literal worked example) - confirms renderGroups()[0]'s own
    // embedded Mesh sub-block actually parses via sr3mesh::MeshBlock::parse
    // with a real g-segment offset (chained the same way
    // LevelMesh::resolveReferencedMeshes() already chains the HEAD's own
    // g-backed meshes), and that its draw ranges' materialId values are
    // in-bounds against materialRecords().size() - i.e. the SAME id space
    // drives both. -------------------------------------------------------
    {
        const std::string wantName = "lite_fixh.clmesh_pc";
        std::string gName2 = "lite_fixh.glmesh_pc";
        std::vector<uint8_t> cb2, gb2;
        if (findEntry(str2Container, wantName, cb2, gb2, gName2) && !cb2.empty() && !gb2.empty()) {
            std::printf("\n===== DEEP TEST: %s (%zu bytes) + %s (%zu bytes) =====\n",
                        wantName.c_str(), cb2.size(), gName2.c_str(), gb2.size());
            vpp::ByteView cv2(cb2.data(), cb2.size());
            vpp::ByteView gv2(gb2.data(), gb2.size());

            // Direct, from-scratch byte scan (not inferred from spec prose):
            // does the literal vehicle/character `0x424BD00D` GeometryBlock
            // magic occur ANYWHERE in this real file's raw bytes, at ANY
            // offset (not just where a naive walk would expect it)? Scans
            // BOTH byte orders in case of a convention mismatch. This is the
            // direct real-byte confirmation the task asked for, independent
            // of LevelMesh's own walk (which never searches for this magic
            // at all - its own header is the unrelated `0x4fe66afa`).
            {
                int hitsLE = 0, hitsBE = 0;
                std::vector<size_t> offsetsLE;
                for (size_t off = 0; off + 4 <= cb2.size(); ++off) {
                    uint32_t v = cv2.readU32LE(off);
                    if (v == 0x424BD00Du) { ++hitsLE; offsetsLE.push_back(off); }
                    uint32_t be = (static_cast<uint32_t>(cb2[off]) << 24) | (static_cast<uint32_t>(cb2[off + 1]) << 16) |
                                  (static_cast<uint32_t>(cb2[off + 2]) << 8) | static_cast<uint32_t>(cb2[off + 3]);
                    if (be == 0x424BD00Du) ++hitsBE;
                }
                std::printf("  [direct byte scan] literal 0x424BD00D magic occurrences in raw file bytes: "
                            "LE=%d BE=%d (file size %zu)\n",
                            hitsLE, hitsBE, cb2.size());
                for (size_t o : offsetsLE) std::printf("    LE hit at offset %zu\n", o);
            }
            try {
                sr3clmesh::LevelMesh lm2 = sr3clmesh::LevelMesh::parse(cv2);
                std::printf("  walkComplete=%d landsOnEof=%d headerOffset=%zu\n",
                            lm2.walkComplete(), lm2.landsOnEof(), lm2.headerOffset());
                std::printf("  refA(head mesh refs).count=%zu\n", lm2.referenceArrayA().count);

                std::vector<sr3mesh::MeshBlock> headBlocks = lm2.resolveReferencedMeshes(cv2, gv2);
                size_t gCursor = 0;
                for (auto& hb : headBlocks) if (hb.bulkInGFile()) gCursor += hb.gLength();
                std::printf("  resolved %zu/%zu head mesh sub-blocks; gCursor after head = %zu\n",
                            headBlocks.size(), lm2.referenceArrayA().count, gCursor);

                const auto& mid2 = lm2.middle();
                std::printf("  renderGroupCount=%zu\n", mid2.renderGroupCount);
                for (size_t g = 0; g < mid2.renderGroupCount; ++g) {
                    const auto& rg = mid2.renderGroups[g];
                    std::printf("  renderGroup[%zu]: offset=%zu mesh.offset=%zu mesh.cLength=%zu indexList.count=%zu\n",
                                g, rg.offset, rg.mesh.offset, rg.mesh.cLength, rg.indexList.count);
                    size_t headerDisplacement = ((rg.mesh.offset + 16 + 7) / 8 * 8) - rg.mesh.offset;
                    try {
                        sr3mesh::MeshBlock rgMesh = sr3mesh::MeshBlock::parse(
                            cv2, rg.mesh.offset, gv2, gCursor, headerDisplacement);
                        std::printf("    PARSED OK: flags=0x%02X bulkInGFile=%d channels=%zu indexCount=%u "
                                    "drawGroupsLocated=%d\n",
                                    rgMesh.flags(), rgMesh.bulkInGFile(), rgMesh.channels().size(),
                                    rgMesh.indexCount(), rgMesh.drawGroupsLocated());
                        if (rgMesh.drawGroupsLocated()) {
                            for (size_t dgi = 0; dgi < rgMesh.drawGroups().size(); ++dgi) {
                                std::printf("    drawGroup[%zu]: %zu range(s):", dgi, rgMesh.drawGroups()[dgi].size());
                                for (const auto& range : rgMesh.drawGroups()[dgi]) {
                                    std::printf(" mat=%u(sub=%u,idx=%u)", range.materialId, range.submeshIndex,
                                                range.indexCount);
                                }
                                std::printf("\n");
                            }
                        }
                        if (rgMesh.bulkInGFile()) gCursor += rgMesh.gLength();
                    } catch (const std::exception& ex) {
                        std::printf("    PARSE FAILED: %s\n", ex.what());
                    }
                }

                // Sanity check on the new public API (LevelMesh::materialTextureBinding),
                // against the exact same real file, comparing it to the shape spec-
                // geometry-format.md Sec4.2's own worked example describes ("one
                // texture reference in this sample (...lightfixtures_d.tga)").
                std::printf("  [API check] LevelMesh::materialTextureBinding() per material:\n");
                for (size_t mi = 0; mi < mid2.materialCount; ++mi) {
                    sr3geometry::MaterialBinding mb = lm2.materialTextureBinding(mi, cv2);
                    std::printf("    material[%zu]: %zu texture(s)", mi, mb.textures.size());
                    for (const auto& t : mb.textures) {
                        std::printf("  [slot=%u hash=0x%08X name='%s']", t.slot, t.paramHash, t.name.c_str());
                    }
                    const std::string* diff = mb.diffuse();
                    const std::string* norm = mb.normalMap();
                    std::printf("  diffuse()=%s normalMap()=%s\n", diff ? diff->c_str() : "(null)",
                                norm ? norm->c_str() : "(null)");
                }
            } catch (const std::exception& ex) {
                std::printf("  LevelMesh::parse FAILED: %s\n", ex.what());
            }
        } else {
            std::printf("\n===== DEEP TEST: lite_fixh.clmesh_pc not found in this str2_pc =====\n");
        }
    }

    return 0;
}
