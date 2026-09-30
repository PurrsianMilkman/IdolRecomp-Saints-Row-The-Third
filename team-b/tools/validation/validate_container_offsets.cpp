// Which rule locates a compressed directory entry's zlib stream?
//
// Motivation (HANDOFF §9.76): decoding `.fxo_pc` through vpp::Container using
// the directory `+0x08` offset gives only 108/844, while
//     physical(i) = payloadStart + sum over earlier entries j of roundup(size_j, 0x800)
// (size_j = `+0x10` for a compressed entry) reproduced each entry's own
// `+0x0C` on 1,693/1,693 top-level `.fxo*` entries. That was measured in ONE
// archive family. This harness tests the candidate rules over EVERY container
// in the shipped data, grouped by the container's flags, so the rule that goes
// into vpp::Container is the general one, not a fit to one archive.
//
// Candidate rules, per compressed entry i (all relative to payloadStart):
//   A  +0x08 (the directory field; what vpp::Container used to do)
//   B  running sum of roundup(+0x10 if compressed else +0x0C, 0x800)
//   C  running sum of roundup(+0x0C, 0x800)   (the offline packer's spacing)
//
// ORACLE, independent of any offset rule. A candidate offset must hold a
// valid zlib stream that inflates to EXACTLY the entry's own `+0x0C` bytes
// with no error, output capped at +0x0C and input capped at +0x10 + 8 (the
// container's own decoder works the same way: these streams do not reliably
// carry a checked Adler-32 trailer, and a stream end is not required). Two
// strengths are reported:
//   loose  produced == +0x0C, no zlib error before that
//   tight  loose AND the number of input bytes the inflater consumed lies in
//          [+0x10 - 6, +0x10] - i.e. the stream ends where the entry's own
//          compressed-size field says it does, a second independent field
//          agreeing with the first.
//
// Usage: validate_container_offsets <archive.vpp_pc> [...]
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <zlib.h>

#include "vpp/container.h"

namespace {

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}

struct Verdict {
    bool loose = false;
    bool tight = false;
};

// The stream must produce EXACTLY `want` bytes and then stop. Output is NOT
// capped at `want`: a cap makes a longer stream (some other entry's data)
// pass as a "prefix", which is precisely how a wrong offset can look like a
// successful decode of the right length (see HANDOFF §9.78). Real entries
// end their deflate data after `want` bytes; the bytes that follow (a short
// sync-flush-style tail, then padding) make inflate() report an error AFTER
// the payload, so any status is accepted as long as total_out == want.
Verdict oracle(const uint8_t* base, size_t total, size_t off, size_t want, size_t len10) {
    Verdict v;
    if (off + 2 > total || want == 0) return v;
    z_stream s{};
    if (inflateInit(&s) != Z_OK) return v;
    std::vector<uint8_t> out(want + 65536);
    const size_t inCap = std::min(total - off, len10 + 64);
    s.next_in = const_cast<Bytef*>(base + off);
    s.avail_in = static_cast<uInt>(inCap);
    s.next_out = out.data();
    s.avail_out = static_cast<uInt>(out.size());
    while (true) {
        const int ret = inflate(&s, Z_NO_FLUSH);
        if (ret != Z_OK) break;      // Z_STREAM_END, or an error after the payload
        if (s.avail_in == 0) break;
    }
    const size_t produced = s.total_out;
    const size_t consumed = inCap - s.avail_in;
    inflateEnd(&s);
    v.loose = produced == want;
    v.tight = v.loose && consumed >= len10 && consumed <= len10 + 8;
    return v;
}

struct Tally {
    long long containers = 0, entries = 0, compressed = 0, raw = 0;
    long long loose[3] = {0, 0, 0};   // rules A, B, C
    long long tight[3] = {0, 0, 0};
    long long anyLoose = 0, noneLoose = 0;
    long long nfN = 0, nfLoose[3] = {0, 0, 0}, nfTight[3] = {0, 0, 0}; // non-first entries
    // Mode (b) shared-stream check (see walk()).
    long long dContainers = 0, dMixed = 0, dPack = 0, dLen = 0, dBoth = 0, dMixedBoth = 0, dShown = 0;
};
std::map<uint32_t, Tally> g_byFlags;
long long g_containers = 0, g_failedOpen = 0;

size_t roundUp800(uint64_t v) { return static_cast<size_t>((v + 0x7FF) / 0x800 * 0x800); }

void walk(vpp::ByteView bytes) {
    vpp::Container c(bytes);
    ++g_containers;
    const vpp::Header& h = c.header();
    Tally& t = g_byFlags[h.flagsRaw];
    ++t.containers;
    const size_t payloadStart = h.payloadStart();

    uint64_t runB = 0, runC = 0;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const vpp::Entry& e = c.entries()[i];
        const vpp::PayloadLocation& loc = e.payload;
        ++t.entries;
        const bool isRaw = loc.kind == vpp::PayloadKind::Raw;
        const size_t sizeB = isRaw ? loc.decompressedLength : loc.length; // +0x10 for compressed
        if (isRaw) {
            ++t.raw;
        } else {
            ++t.compressed;
            const size_t want = loc.decompressedLength;
            const size_t len10 = loc.length;
            const size_t off[3] = {loc.offset,                                   // A
                                   payloadStart + static_cast<size_t>(runB),     // B
                                   payloadStart + static_cast<size_t>(runC)};    // C
            Verdict v[3];
            bool any = false;
            for (int r = 0; r < 3; ++r) {
                v[r] = oracle(bytes.data(), bytes.size(), off[r], want, len10);
                t.loose[r] += v[r].loose;
                t.tight[r] += v[r].tight;
                any = any || v[r].loose;
                if (i > 0) { t.nfLoose[r] += v[r].loose; t.nfTight[r] += v[r].tight; }
            }
            if (any) ++t.anyLoose; else ++t.noneLoose;
            if (i > 0) ++t.nfN;
        }
        runB += roundUp800(sizeB);
        runC += roundUp800(loc.decompressedLength);
    }

    // Mode (b) (flags bit 0x2): ONE zlib stream at payloadStart spans every
    // compressed entry; entry i is the slice [+0x08, +0x08 + +0x0C) of its
    // decompressed output (spec §3.2b), with +0x08 the running sum of the
    // earlier compressed entries' +0x0C (packed, no padding). Oracle: the
    // stream, decoded from payloadStart, ends after EXACTLY sum(+0x0C) bytes,
    // and every compressed entry's +0x08 equals that running sum.
    if ((h.flagsRaw & 2) != 0) {
        uint64_t sumC = 0, run = 0;
        bool packOk = true;
        size_t nComp = 0, nRaw = 0;
        for (size_t i = 0; i < c.entries().size(); ++i) {
            const vpp::PayloadLocation& loc = c.entries()[i].payload;
            if (loc.kind == vpp::PayloadKind::Raw) { ++nRaw; continue; }
            ++nComp;
            if (loc.offset - payloadStart != run) packOk = false;
            run += loc.decompressedLength;
            sumC += loc.decompressedLength;
        }
        if (nComp > 0) {
            ++t.dContainers;
            if (nRaw > 0) ++t.dMixed;
            z_stream s{};
            if (inflateInit(&s) == Z_OK) {
                std::vector<uint8_t> out(static_cast<size_t>(sumC) + 65536);
                s.next_in = const_cast<Bytef*>(bytes.data() + payloadStart);
                s.avail_in = static_cast<uInt>(bytes.size() - payloadStart);
                s.next_out = out.data();
                s.avail_out = static_cast<uInt>(out.size());
                while (true) {
                    const int ret = inflate(&s, Z_NO_FLUSH);
                    if (ret != Z_OK) break;
                    if (s.avail_in == 0) break;
                }
                const bool lenOk = s.total_out == sumC;
                inflateEnd(&s);
                t.dPack += packOk;
                t.dLen += lenOk;
                t.dBoth += (packOk && lenOk);
                if (nRaw > 0) { t.dMixedBoth += (packOk && lenOk); }
                if (!(packOk && lenOk) && t.dShown < 4) {
                    ++t.dShown;
                    std::printf("  mode-b container failing: entries %zu (compressed %zu, raw %zu) packOk %d lenOk %d sumC %llu\n",
                                c.entries().size(), nComp, nRaw, packOk ? 1 : 0, lenOk ? 1 : 0,
                                static_cast<unsigned long long>(sumC));
                }
            }
        }
    }

    // Recurse into raw entries that are themselves containers.
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        try {
            walk(c.rawEntryBytes(i));
        } catch (const std::exception&) {
            // not a container - expected for ordinary raw payloads
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> b = readFile(argv[i]);
        if (b.empty()) continue;
        try {
            walk(vpp::ByteView(b.data(), b.size()));
        } catch (const std::exception& e) {
            ++g_failedOpen;
            std::printf("open failed %s: %s\n", argv[i], e.what());
        }
        std::printf("scanned %s\n", argv[i]);
        std::fflush(stdout);
    }
    std::printf("\ncontainers walked: %lld (top-level open failures %lld)\n", g_containers, g_failedOpen);
    std::printf("\n=== per flags value; oracle = zlib inflates to exactly +0x0C (loose), and consumes +0x10 +-6 input bytes (tight) ===\n");
    for (const auto& kv : g_byFlags) {
        const Tally& t = kv.second;
        if (t.compressed == 0) continue;
        std::printf("flags 0x%08X: containers %lld, entries %lld (compressed %lld, raw %lld)\n", kv.first,
                    t.containers, t.entries, t.compressed, t.raw);
        std::printf("    ALL compressed entries : loose A %lld  B %lld  C %lld   | tight A %lld  B %lld  C %lld   | no rule locates it: %lld\n",
                    t.loose[0], t.loose[1], t.loose[2], t.tight[0], t.tight[1], t.tight[2], t.noneLoose);
        std::printf("    non-first entries (%lld): loose A %lld  B %lld  C %lld   | tight A %lld  B %lld  C %lld\n", t.nfN,
                    t.nfLoose[0], t.nfLoose[1], t.nfLoose[2], t.nfTight[0], t.nfTight[1], t.nfTight[2]);
        if (t.dContainers > 0) {
            std::printf("    MODE (b) shared stream (bit 0x2), containers with >=1 compressed entry: %lld (mixed with raw entries: %lld)\n"
                        "        +0x08 == running sum of earlier compressed +0x0C : %lld / %lld\n"
                        "        stream from payloadStart ends after exactly sum(+0x0C) bytes: %lld / %lld\n"
                        "        BOTH: %lld / %lld   (of the mixed ones: %lld / %lld)\n",
                        t.dContainers, t.dMixed, t.dPack, t.dContainers, t.dLen, t.dContainers,
                        t.dBoth, t.dContainers, t.dMixedBoth, t.dMixed);
        }
    }
    std::printf("\n(flags values with no compressed entry are omitted)\n");
    return 0;
}
