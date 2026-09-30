// Population census of vpp::Container::decompressEntry over EVERY container.
//
// HANDOFF §9.78 replaced the +0x08 stream-location rule with the two rules the
// data actually obeys (mode (a): running sum of round_up(+0x10, 0x800); mode
// (b): one shared stream sliced at +0x08). This harness is the end-to-end
// check, and its oracles are independent of the offset arithmetic:
//   * every compressed entry's decode status (Ok / error), by container mode;
//   * the CONTENT check that used to catch the old bug: every decoded
//     .xtbl / .cte_xtbl must be well-formed XML (vpp::refineWithXtblValidation)
//     - the old decoder returned truncated-mid-tag prefixes of a neighbour's
//     data for these;
//   * every decoded .fxo_pc must open with the wrapper magic 0x4B42A1EE, and
//     every .cpeg_pc / .cvbm_pc c-file must carry the PEG header's fixed words
//     (0x10 == 0x14 and 0x12 == 0 in its first 0x18 bytes, spec-texture-format.md).
//
// Usage: validate_container_decode <archive.vpp_pc> [...]
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "vpp/container.h"
#include "vpp/content_validation.h"

namespace {

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}

bool endsWith(const std::string& s, const char* x) {
    const size_t n = std::strlen(x);
    return s.size() >= n && s.compare(s.size() - n, n, x) == 0;
}

const char* statusName(vpp::DecodeStatus s) {
    switch (s) {
        case vpp::DecodeStatus::Ok: return "Ok";
        case vpp::DecodeStatus::OkUnconfirmedContent: return "OkUnconfirmedContent";
        case vpp::DecodeStatus::ContentValidated: return "ContentValidated";
        case vpp::DecodeStatus::ContentValidationFailed: return "ContentValidationFailed";
        case vpp::DecodeStatus::RecoveredSharedStream: return "RecoveredSharedStream";
        case vpp::DecodeStatus::RecoveredSharedStreamLongChain: return "RecoveredSharedStreamLongChain";
        case vpp::DecodeStatus::NoValidZlibHeaderAtOffset: return "NoValidZlibHeaderAtOffset";
        case vpp::DecodeStatus::ZlibStreamError: return "ZlibStreamError";
        case vpp::DecodeStatus::SizeMismatch: return "SizeMismatch";
    }
    return "?";
}

// A real XML well-formedness check (balanced, case-exact tag matching, exactly
// one root element, only whitespace/comments/declarations outside it). The
// container's own refineWithXtblValidation() additionally demands that the
// root's first child is <Table> (spec-xtbl-format.md §2) - a heuristic that
// stood in for corruption detection while the offset rule was unknown, and
// which legitimately fails on tables of another shape (a time-of-day table
// has <east0> there). This check is the correct content oracle.
bool wellFormedXml(const std::vector<uint8_t>& d, bool* hasTable) {
    std::vector<std::string> stack;
    size_t i = 0;
    const size_t n = d.size();
    bool sawRoot = false, closedRoot = false;
    if (hasTable) *hasTable = false;
    auto ws = [](uint8_t c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    if (n >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) i = 3;
    while (i < n) {
        if (d[i] != '<') {
            if (stack.empty() && !ws(d[i])) return false; // text outside the root
            ++i;
            continue;
        }
        if (i + 3 < n && d[i + 1] == '!' && d[i + 2] == '-' && d[i + 3] == '-') {
            size_t j = i + 4;
            while (j + 2 < n && !(d[j] == '-' && d[j + 1] == '-' && d[j + 2] == '>')) ++j;
            if (j + 2 >= n) return false;
            i = j + 3;
            continue;
        }
        if (i + 1 < n && (d[i + 1] == '?' || d[i + 1] == '!')) {
            size_t j = i + 2;
            while (j < n && d[j] != '>') ++j;
            if (j >= n) return false;
            i = j + 1;
            continue;
        }
        bool closing = i + 1 < n && d[i + 1] == '/';
        size_t j = i + (closing ? 2 : 1);
        const size_t nameStart = j;
        while (j < n && !ws(d[j]) && d[j] != '>' && d[j] != '/') ++j;
        std::string name(reinterpret_cast<const char*>(d.data()) + nameStart, j - nameStart);
        if (name.empty()) return false;
        bool selfClose = false;
        char quote = 0;
        while (j < n) { // skip attributes to the closing '>'
            const uint8_t ch = d[j];
            if (quote) { if (ch == static_cast<uint8_t>(quote)) quote = 0; }
            else if (ch == '"' || ch == '\'') quote = static_cast<char>(ch);
            else if (ch == '>') break;
            else if (ch == '/' && j + 1 < n && d[j + 1] == '>') selfClose = true;
            ++j;
        }
        if (j >= n) return false; // truncated inside a tag
        if (closing) {
            if (stack.empty() || stack.back() != name) return false;
            stack.pop_back();
            if (stack.empty()) closedRoot = true;
        } else {
            if (stack.empty()) {
                if (sawRoot) return false; // a second root
                sawRoot = true;
            } else if (hasTable && stack.size() == 1 && stack.back() == "root" &&
                       (name == "Table" || name == "table")) {
                *hasTable = true;
            }
            if (!selfClose) stack.push_back(name);
            else if (stack.empty()) closedRoot = true;
        }
        i = j + 1;
    }
    return sawRoot && closedRoot && stack.empty();
}

struct Tally {
    long long containers = 0, compressed = 0;
    std::map<std::string, long long> status;
    long long nonFirst = 0, nonFirstOk = 0;
};
Tally g_modeA, g_modeB;
long long g_xtbl = 0, g_xtblWellFormed = 0, g_xtblMalformed = 0, g_xtblNotDecoded = 0;
long long g_xtblTableShaped = 0, g_xtblRefinerAccepts = 0;
long long g_fxo = 0, g_fxoMagic = 0, g_fxoNotDecoded = 0;
long long g_peg = 0, g_pegHeaderOk = 0, g_pegNotDecoded = 0;
std::vector<std::string> g_examplesBad;
long long g_containers = 0;

void walk(vpp::ByteView bytes, const std::string& path) {
    vpp::Container c(bytes);
    ++g_containers;
    Tally& t = c.header().isSharedStreamMode() ? g_modeB : g_modeA;
    ++t.containers;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const vpp::Entry& e = c.entries()[i];
        if (e.payload.kind != vpp::PayloadKind::Compressed) continue;
        ++t.compressed;
        vpp::DecompressResult r = c.decompressEntry(i);
        ++t.status[statusName(r.status)];
        const bool ok = r.status == vpp::DecodeStatus::Ok;
        if (i > 0) { ++t.nonFirst; t.nonFirstOk += ok; }

        if (vpp::looksLikeXtblFilename(e.name)) {
            ++g_xtbl;
            if (!ok) { ++g_xtblNotDecoded; continue; }
            bool hasTable = false;
            if (wellFormedXml(r.data, &hasTable)) {
                ++g_xtblWellFormed;
                g_xtblTableShaped += hasTable;
            } else {
                ++g_xtblMalformed;
                if (g_examplesBad.size() < 8) g_examplesBad.push_back(path + " :: " + e.name);
            }
            vpp::DecompressResult forCheck = r;
            forCheck.status = vpp::DecodeStatus::OkUnconfirmedContent; // the refiner's input state
            g_xtblRefinerAccepts +=
                vpp::refineWithXtblValidation(std::move(forCheck)).status ==
                vpp::DecodeStatus::ContentValidated;
        } else if (endsWith(e.name, ".fxo_pc")) {
            ++g_fxo;
            if (!ok) { ++g_fxoNotDecoded; continue; }
            uint32_t magic = 0;
            if (r.data.size() >= 4) std::memcpy(&magic, r.data.data(), 4);
            g_fxoMagic += (magic == 0x4B42A1EEu);
        } else if (endsWith(e.name, ".cpeg_pc") || endsWith(e.name, ".cvbm_pc")) {
            ++g_peg;
            if (!ok) { ++g_pegNotDecoded; continue; }
            if (r.data.size() >= 0x18) {
                uint16_t w10, w12, w14;
                std::memcpy(&w10, r.data.data() + 0x10, 2);
                std::memcpy(&w12, r.data.data() + 0x12, 2);
                std::memcpy(&w14, r.data.data() + 0x14, 2);
                g_pegHeaderOk += (w10 == w14 && w12 == 0);
            }
        }
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        try {
            walk(c.rawEntryBytes(i), path + "/" + c.entries()[i].name);
        } catch (const std::exception&) {
        }
    }
}

void report(const char* label, const Tally& t) {
    std::printf("%s: containers %lld, compressed entries %lld\n", label, t.containers, t.compressed);
    for (const auto& kv : t.status) std::printf("    %-28s %lld\n", kv.first.c_str(), kv.second);
    std::printf("    non-first entries Ok: %lld / %lld\n", t.nonFirstOk, t.nonFirst);
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> b = readFile(argv[i]);
        if (b.empty()) continue;
        try {
            walk(vpp::ByteView(b.data(), b.size()), argv[i]);
        } catch (const std::exception& e) {
            std::printf("open failed %s: %s\n", argv[i], e.what());
        }
        std::printf("scanned %s\n", argv[i]);
        std::fflush(stdout);
    }
    std::printf("\ncontainers walked: %lld\n\n", g_containers);
    report("MODE (a) containers (flags bit 0x2 clear)", g_modeA);
    report("MODE (b) containers (flags bit 0x2 set)", g_modeB);
    std::printf("\nCONTENT oracles on decoded entries:\n");
    std::printf("  .xtbl/.cte_xtbl well-formed XML : %lld / %lld   (malformed %lld, not decoded %lld)\n",
                g_xtblWellFormed, g_xtbl, g_xtblMalformed, g_xtblNotDecoded);
    std::printf("     of which <root><Table>-shaped : %lld   (the old spec-derived refiner accepts %lld: it rejects other shapes)\n",
                g_xtblTableShaped, g_xtblRefinerAccepts);
    for (const std::string& s : g_examplesBad) std::printf("     malformed: %s\n", s.c_str());
    std::printf("  .fxo_pc wrapper magic 0x4B42A1EE  : %lld / %lld   (not decoded %lld)\n", g_fxoMagic, g_fxo,
                g_fxoNotDecoded);
    std::printf("  .cpeg_pc/.cvbm_pc header words     : %lld / %lld   (not decoded %lld)\n", g_pegHeaderOk, g_peg,
                g_pegNotDecoded);
    return 0;
}
