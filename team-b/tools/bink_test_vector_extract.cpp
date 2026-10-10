// Bink real-data test-vector extractor: parses spec-bink-video-test-vectors.md
// (a DATA-ONLY spec appendix, explicitly flagged safe to read directly per
// its own top note - "no code, no narrative analysis", published by Team A
// from a real Unicorn-CPU-emulation capture of binkw32.dll's own execution,
// not from reading the executable or any disassembly) and writes out every
// one of its 22 raw hex-dumped blobs as real binary files, verifying EACH
// blob's SHA-256 against BOTH the per-blob header hash AND the CSV index
// table's own hash before writing it - per this task's own explicit
// instruction to independently re-verify the spec file's hashes rather than
// trust them blindly, even though the orchestrating session already did so
// once.
//
// This is the same "regenerate a gitignored tests/golden/ fixture from a
// committed, human-readable source" convention tools/bink_frame0_probe.cpp
// already established for tests/golden/bink_logo_frame0/frame0_raw.bin -
// the 22 binary blobs themselves are NOT committed to git (consistent with
// .gitignore's own "Large real-data test fixtures... binary, regenerated
// locally" rule); this tool is how a developer (or CI) regenerates them from
// the spec file that IS committed.
//
// Usage: bink_test_vector_extract <spec-bink-video-test-vectors.md> <outDir>
//
// Exit code 0 only if every blob in the CSV index was found, extracted, and
// passed both hash checks. Any mismatch is reported by name and the tool
// exits nonzero - no blob is silently skipped or substituted.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "bink_sha256.h"

namespace {

struct IndexEntry {
    std::string file;
    size_t sizeBytes = 0;
    std::string sha256;
};

// Parses the one ```csv ... ``` fenced block (the "Index table" section),
// columns: file,size_bytes,sha256,spec_section,notes (the last two may
// contain embedded commas inside quotes - only the first 3 comma-delimited
// fields are needed here, so this deliberately does NOT implement a full
// CSV-with-quoted-commas parser, it just splits on the first two commas).
std::map<std::string, IndexEntry> parseCsvIndex(const std::vector<std::string>& lines) {
    std::map<std::string, IndexEntry> out;
    bool inCsv = false;
    for (const auto& line : lines) {
        if (line.rfind("```csv", 0) == 0) {
            inCsv = true;
            continue;
        }
        if (inCsv && line.rfind("```", 0) == 0) {
            inCsv = false;
            continue;
        }
        if (!inCsv) continue;
        if (line.rfind("file,size_bytes,sha256", 0) == 0) continue;
        if (line.empty()) continue;

        const size_t c1 = line.find(',');
        if (c1 == std::string::npos) continue;
        const size_t c2 = line.find(',', c1 + 1);
        if (c2 == std::string::npos) continue;
        const size_t c3 = line.find(',', c2 + 1);
        if (c3 == std::string::npos) continue;

        IndexEntry e;
        e.file = line.substr(0, c1);
        e.sizeBytes = static_cast<size_t>(std::stoull(line.substr(c1 + 1, c2 - c1 - 1)));
        e.sha256 = line.substr(c2 + 1, c3 - c2 - 1);
        out[e.file] = e;
    }
    return out;
}

// Parses one "### `filename` (N bytes, sha256 `HASH`)" header line, returning
// true and filling out the three captured fields on a match.
bool parseBlobHeader(const std::string& line, std::string& fname, size_t& hdrSize, std::string& hdrSha) {
    const std::string prefix = "### `";
    if (line.rfind(prefix, 0) != 0) return false;
    const size_t nameEnd = line.find('`', prefix.size());
    if (nameEnd == std::string::npos) return false;
    fname = line.substr(prefix.size(), nameEnd - prefix.size());

    const size_t parenOpen = line.find('(', nameEnd);
    if (parenOpen == std::string::npos) return false;
    const size_t bytesPos = line.find("bytes", parenOpen);
    if (bytesPos == std::string::npos) return false;
    const std::string sizeStr = line.substr(parenOpen + 1, bytesPos - parenOpen - 1);
    hdrSize = static_cast<size_t>(std::stoull(sizeStr));

    const size_t shaBacktick1 = line.find('`', bytesPos);
    if (shaBacktick1 == std::string::npos) return false;
    const size_t shaBacktick2 = line.find('`', shaBacktick1 + 1);
    if (shaBacktick2 == std::string::npos) return false;
    hdrSha = line.substr(shaBacktick1 + 1, shaBacktick2 - shaBacktick1 - 1);
    return true;
}

// One hex-dump line looks like "000000: 3300fdffffff...00" - an offset,
// a colon, then a run of hex nibbles (no spaces, per this spec file's own
// "32 bytes/line" convention - though this parser does not depend on the
// exact per-line byte count, it just concatenates every hex run found).
bool parseHexLine(const std::string& line, std::string& hexOut) {
    const size_t colon = line.find(':');
    if (colon == std::string::npos) return false;
    // Offset field must be hex digits only (rules out other ':'-containing lines).
    for (size_t i = 0; i < colon; ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(line[i]))) return false;
    }
    std::string rest = line.substr(colon + 1);
    // Trim whitespace.
    size_t start = rest.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return true;  // empty hex payload, not an error
    size_t end = rest.find_last_not_of(" \t\r\n");
    rest = rest.substr(start, end - start + 1);
    for (char c : rest) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    hexOut = rest;
    return true;
}

std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return out;
}

bool writeFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    if (!bytes.empty()) f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: bink_test_vector_extract <spec-bink-video-test-vectors.md> <outDir>\n");
        return 1;
    }
    const std::string specPath = argv[1];
    const std::string outDir = argv[2];

    std::ifstream f(specPath);
    if (!f) {
        std::printf("FATAL: could not open '%s'\n", specPath.c_str());
        return 1;
    }
    std::vector<std::string> lines;
    {
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
    }

    const auto index = parseCsvIndex(lines);
    std::printf("parsed CSV index: %zu entries\n", index.size());

    int okCount = 0, failCount = 0;
    std::vector<std::string> seen;

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string fname, hdrSha;
        size_t hdrSize = 0;
        if (!parseBlobHeader(lines[i], fname, hdrSize, hdrSha)) continue;

        // Find the next ``` fence, then collect hex lines until the closing ```.
        size_t j = i + 1;
        while (j < lines.size() && lines[j] != "```") ++j;
        ++j;  // past opening fence
        std::string hex;
        bool parseError = false;
        while (j < lines.size() && lines[j] != "```") {
            if (!lines[j].empty()) {
                std::string piece;
                if (parseHexLine(lines[j], piece)) {
                    hex += piece;
                } else {
                    std::printf("WARN: unexpected non-hex line inside %s's blob: '%s'\n", fname.c_str(),
                                lines[j].c_str());
                    parseError = true;
                }
            }
            ++j;
        }
        i = j;

        seen.push_back(fname);
        if (parseError) {
            std::printf("FAIL %s: unparseable blob content\n", fname.c_str());
            ++failCount;
            continue;
        }

        const std::vector<uint8_t> bytes = hexToBytes(hex);
        const std::string actualSha = bink_test_sha256::computeSha256Hex(bytes.data(), bytes.size());

        std::vector<std::string> problems;
        if (bytes.size() != hdrSize) {
            problems.push_back("header-size mismatch (header=" + std::to_string(hdrSize) +
                                " actual=" + std::to_string(bytes.size()) + ")");
        }
        if (actualSha != hdrSha) {
            problems.push_back("header-sha256 mismatch (header=" + hdrSha + " actual=" + actualSha + ")");
        }
        auto it = index.find(fname);
        if (it == index.end()) {
            problems.push_back("NOT FOUND in CSV index");
        } else {
            if (it->second.sizeBytes != bytes.size()) {
                problems.push_back("CSV-size mismatch (csv=" + std::to_string(it->second.sizeBytes) +
                                    " actual=" + std::to_string(bytes.size()) + ")");
            }
            if (it->second.sha256 != actualSha) {
                problems.push_back("CSV-sha256 mismatch (csv=" + it->second.sha256 + " actual=" + actualSha + ")");
            }
        }

        if (!problems.empty()) {
            std::string joined;
            for (size_t k = 0; k < problems.size(); ++k) {
                if (k) joined += "; ";
                joined += problems[k];
            }
            std::printf("FAIL %s: %s\n", fname.c_str(), joined.c_str());
            ++failCount;
            continue;
        }

        const std::string outPath = outDir + "/" + fname;
        if (!writeFile(outPath, bytes)) {
            std::printf("FAIL %s: could not write '%s'\n", fname.c_str(), outPath.c_str());
            ++failCount;
            continue;
        }
        std::printf("OK   %-55s size=%-6zu sha256=%s\n", fname.c_str(), bytes.size(), actualSha.c_str());
        ++okCount;
    }

    for (const auto& kv : index) {
        if (std::find(seen.begin(), seen.end(), kv.first) == seen.end()) {
            std::printf("FAIL %s: listed in CSV index but no matching blob found in the document\n",
                        kv.first.c_str());
            ++failCount;
        }
    }

    std::printf("\n%d OK, %d FAIL (of %zu CSV index entries)\n", okCount, failCount, index.size());
    return failCount == 0 ? 0 : 1;
}
