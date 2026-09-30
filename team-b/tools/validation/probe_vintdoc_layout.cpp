// Throwaway investigation tool (this session's task, 2026-09-30): dumps raw
// header fields and tests candidate hypotheses for how the main structural
// cursor skips past the string pool's character data to reach the
// critical-resource section, for a handful of real .vint_doc files pulled
// from interface_startup.vpp_pc. spec-vint-doc-format.md Sec3.1 confirms the
// per-string resolution formula (base = cursor right after the offset array,
// string = base + offsetArray[index]) but explicitly leaves OPEN how far the
// string pool's raw character data actually extends before the next section
// begins - this tool measures that empirically against real bytes rather
// than guessing.
//
// Kept in-tree per this project's own practice of keeping the tool that
// found a real result (see tools/clmesh_probe.cpp's own precedent).
//
// Usage: probe_vintdoc_layout <interface_startup.vpp_pc> [maxFiles]

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

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

bool endsWith(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
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

void walk(const vpp::Container& c, const std::string& prefix,
          std::vector<std::pair<std::string, std::vector<uint8_t>>>& out, int& remaining) {
    for (size_t i = 0; i < c.entries().size() && remaining > 0; ++i) {
        const std::string& n = c.entries()[i].name;
        if (endsWith(n, ".vint_doc")) {
            std::vector<uint8_t> b;
            if (entryBytes(c, i, b) && !b.empty()) {
                out.emplace_back(prefix + n, std::move(b));
                --remaining;
            }
        }
    }
    for (size_t i = 0; i < c.entries().size() && remaining > 0; ++i) {
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                vpp::Container nested = c.openNested(i);
                walk(nested, prefix + c.entries()[i].name + "/", out, remaining);
            } catch (const std::exception&) {
            }
        }
    }
}

uint32_t rdU32(const std::vector<uint8_t>& b, size_t off) {
    return static_cast<uint32_t>(b[off]) | (static_cast<uint32_t>(b[off + 1]) << 8) |
           (static_cast<uint32_t>(b[off + 2]) << 16) | (static_cast<uint32_t>(b[off + 3]) << 24);
}
uint16_t rdU16(const std::vector<uint8_t>& b, size_t off) {
    return static_cast<uint16_t>(static_cast<uint16_t>(b[off]) | (static_cast<uint16_t>(b[off + 1]) << 8));
}

std::string cstrAt(const std::vector<uint8_t>& b, size_t off, size_t maxLen = 64) {
    if (off >= b.size()) return "<OOB>";
    std::string s;
    size_t i = off;
    while (i < b.size() && b[i] != 0 && s.size() < maxLen) {
        unsigned char c = b[i];
        if (c >= 0x20 && c < 0x7f) s.push_back(static_cast<char>(c));
        else s += "\\x" + std::to_string((int)c);
        ++i;
    }
    if (i < b.size() && b[i] != 0) s += "...";
    return s;
}

bool isPrintableCString(const std::vector<uint8_t>& b, size_t off, size_t& lenOut) {
    if (off >= b.size()) return false;
    size_t i = off;
    while (i < b.size() && b[i] != 0) {
        if (b[i] < 0x09 || (b[i] > 0x0d && b[i] < 0x20) || b[i] >= 0x7f) return false;
        ++i;
        if (i - off > 4096) return false;
    }
    if (i >= b.size()) return false; // no terminator found
    lenOut = i - off;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: probe_vintdoc_layout <interface_startup.vpp_pc> [maxFiles]\n");
        return 1;
    }
    int maxFiles = argc > 2 ? atoi(argv[2]) : 5;

    std::vector<uint8_t> archiveBytes = readFile(argv[1]);
    if (archiveBytes.empty()) {
        printf("could not read %s\n", argv[1]);
        return 1;
    }

    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    int remaining = maxFiles;
    try {
        vpp::Container root(vpp::ByteView(archiveBytes.data(), archiveBytes.size()));
        walk(root, "", files, remaining);
    } catch (const std::exception& ex) {
        printf("FAILED to open archive: %s\n", ex.what());
        return 1;
    }

    printf("found %zu .vint_doc file(s) (cap %d)\n\n", files.size(), maxFiles);

    for (auto& kv : files) {
        const std::string& name = kv.first;
        const std::vector<uint8_t>& b = kv.second;
        printf("=== %s (%zu bytes) ===\n", name.c_str(), b.size());
        if (b.size() < 0x1E) { printf("  too small for header\n\n"); continue; }

        uint32_t magic = rdU32(b, 0x00);
        uint32_t reserved04 = rdU32(b, 0x04);
        uint16_t version = rdU16(b, 0x08);
        uint32_t unk0A = rdU32(b, 0x0A);
        uint32_t metaCount = rdU32(b, 0x0E);
        uint32_t critCount = rdU32(b, 0x12);
        uint32_t secOffset = rdU32(b, 0x16);
        uint16_t elemCount = rdU16(b, 0x1A);
        uint16_t animCount = rdU16(b, 0x1C);

        printf("  magic=0x%08X reserved04=0x%08X version=%u unk0A=0x%08X(%.6f)\n", magic, reserved04,
               version, unk0A, *reinterpret_cast<const float*>(&unk0A));
        printf("  metaCount=%u critCount=%u secOffset(0x16)=0x%X(%u) elemCount=%u animCount=%u\n",
               metaCount, critCount, secOffset, secOffset, elemCount, animCount);

        if (magic != 0x00003027u) { printf("  BAD MAGIC, skipping\n\n"); continue; }

        size_t cursor = 0x1E;
        if (cursor + 4 > b.size()) { printf("  too small for string count\n\n"); continue; }
        uint32_t strCount = rdU32(b, cursor);
        cursor += 4;
        printf("  stringCount=%u\n", strCount);
        {
            printf("  raw hex from 0x1E (count) for 96 bytes:\n   ");
            for (size_t i = 0x1E; i < 0x1E + 96 && i < b.size(); ++i) {
                printf(" %02X", b[i]);
                if ((i - 0x1E) % 16 == 15) printf("\n   ");
            }
            printf("\n");
        }
        if (cursor + static_cast<size_t>(strCount) * 4 > b.size()) {
            printf("  string offset array runs past EOF, skipping\n\n");
            continue;
        }
        std::vector<uint32_t> offs(strCount);
        for (uint32_t i = 0; i < strCount; ++i) offs[i] = rdU32(b, cursor + i * 4);
        cursor += static_cast<size_t>(strCount) * 4;
        size_t base = cursor; // CONFIRMED per spec: cursor position right after offset array

        printf("  base (cursor after offset array) = 0x%zX (%zu)\n", base, base);
        printf("  header secOffset field (0x16) as ABS file offset = 0x%X (%u)  [candidate A]\n",
               secOffset, secOffset);

        // Resolve first several strings and find max reach.
        size_t maxReach = 0;
        int cleanCount = 0, dirtyCount = 0;
        printf("  first strings resolved via base+offset:\n");
        for (uint32_t i = 0; i < strCount; ++i) {
            size_t at = base + offs[i];
            size_t len = 0;
            bool clean = isPrintableCString(b, at, len);
            if (clean) {
                ++cleanCount;
                if (at + len + 1 > maxReach) maxReach = at + len + 1;
            } else {
                ++dirtyCount;
            }
            if (i < 10) {
                printf("    [%u] off=%u at=0x%zx -> \"%s\" %s\n", i, offs[i], at,
                       cstrAt(b, at).c_str(), clean ? "(clean)" : "(NOT clean nul-terminated printable)");
            }
        }
        printf("  string resolution: clean=%d dirty=%d / %u  maxReach(candidate B)=0x%zx (%zu)\n",
               cleanCount, dirtyCount, strCount, maxReach, maxReach);

        // Candidate C: literally no skip at all - critical resources start
        // right at base (cursor untouched beyond the offset array).
        printf("  candidate C (no skip, critRes starts at base 0x%zx):\n", base);

        auto tryCriticalResourcesAt = [&](const char* label, size_t start) {
            if (start > b.size()) { printf("    %s: start 0x%zx is past EOF (size %zu)\n", label, start, b.size()); return; }
            size_t c2 = start;
            printf("    %s: start=0x%zx, critCount=%u, version=%u\n", label, start, critCount, version);
            bool ok = true;
            for (uint32_t i = 0; i < critCount && i < 20; ++i) {
                size_t recSize = 1 + 4 + (version == 2 ? 1 : 0);
                if (c2 + recSize > b.size()) { printf("      entry %u runs past EOF at 0x%zx\n", i, c2); ok = false; break; }
                uint8_t selector = b[c2];
                uint32_t rawVal = rdU32(b, c2 + 1);
                printf("      entry %u: selector=%u rawVal=0x%08X\n", i, selector, rawVal);
                c2 += recSize;
            }
            if (ok) {
                printf("      after critRes, cursor=0x%zx; next %u metadata entries (2xu32 string idx each):\n", c2, metaCount);
                for (uint32_t i = 0; i < metaCount && i < 10; ++i) {
                    if (c2 + 8 > b.size()) { printf("        entry %u runs past EOF\n", i); break; }
                    uint32_t nameIdx = rdU32(b, c2);
                    uint32_t valIdx = rdU32(b, c2 + 8 > b.size() ? c2 : c2 + 4);
                    bool nameInRange = nameIdx < strCount;
                    bool valInRange = valIdx < strCount;
                    printf("        entry %u: nameIdx=%u(%s) valIdx=%u(%s) name=\"%s\" val=\"%s\"\n", i,
                           nameIdx, nameInRange ? "in-range" : "OOR", valIdx, valInRange ? "in-range" : "OOR",
                           nameInRange ? cstrAt(b, base + offs[nameIdx]).c_str() : "?",
                           valInRange ? cstrAt(b, base + offs[valIdx]).c_str() : "?");
                    c2 += 8;
                }
                printf("      cursor after metadata = 0x%zx / filesize 0x%zx\n", c2, b.size());
            }
        };

        tryCriticalResourcesAt("A(secOffset)", secOffset);
        tryCriticalResourcesAt("B(maxReach)", maxReach);
        tryCriticalResourcesAt("C(base, no skip)", base);

        printf("\n");
    }

    return 0;
}
