// Real-data validation of the .csc_pc sampler section (spec-cutscene-camera-
// format.md Sec10), written from the spec text ONLY.
//
// PART 1 (everything up to "PART 2") is an INDEPENDENT decode: it does not
// call sr3cutscene at all. It walks every archive given on the command line
// (nested containers, same walk as validate_binding_roles), collects every
// .csc_pc entry, parses each with its own memcpy-based reader (Sec3-Sec5),
// decodes channel 1 as four s16 (x,y,z,w) * 16385/2^28 (Sec10.4) and runs
// the claims of Sec10.7 - each paired with a CONTROL that is expected to
// fail. PART 2 then runs the library's new accessors and sampler
// (sr3cutscene::decodeQuaternions*, sampleShot, ...) against the independent
// decode as a differential test between two implementations.
//
// Statistics are over DISTINCT files (identical bytes counted once), because
// spec Sec2 says every duplicate entry is byte-identical; the all-entries
// count is printed too.
//
// Usage: validate_csc_sampler <archive.vpp_pc>...
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "sr3cutscene/camera_script.h"
#include "vpp/container.h"

namespace {

// ---------------------------------------------------------------- corpus --

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    std::streamsize size = f.tellg();
    if (size <= 0) return {};
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

std::string lower(std::string s) {
    for (auto& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

bool endsWith(const std::string& s, const std::string& x) {
    return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}

uint64_t fnv1a(const std::vector<uint8_t>& b) {
    uint64_t h = 1469598103934665603ull;
    for (uint8_t c : b) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h ^ (static_cast<uint64_t>(b.size()) << 1);
}

struct Entry {
    std::string archive, name;
    uint64_t hash = 0;
    size_t size = 0;
};

std::vector<Entry> g_entries;                          // every .csc_pc entry read
std::map<uint64_t, std::vector<uint8_t>> g_distinct;   // content hash -> bytes
std::map<std::string, std::set<uint64_t>> g_hashesByName;
int g_unreadableCscNamed = 0;                          // .csc_pc-named entries the container could not decode
std::vector<std::string> g_unreadableNames;
long long g_entriesSeen = 0;
std::string g_currentArchive;

void take(const std::string& name, const uint8_t* p, size_t n) {
    std::vector<uint8_t> b(p, p + n);
    Entry e;
    e.archive = g_currentArchive;
    e.name = name;
    e.hash = fnv1a(b);
    e.size = n;
    g_entries.push_back(e);
    g_hashesByName[lower(name)].insert(e.hash);
    if (!g_distinct.count(e.hash)) g_distinct.emplace(e.hash, std::move(b));
}

void walk(const vpp::Container& c, int depth) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const auto& e = c.entries()[i];
        ++g_entriesSeen;
        const bool isCsc = endsWith(lower(e.name), ".csc_pc");
        try {
            if (e.payload.kind == vpp::PayloadKind::Raw) {
                if (depth < 8) {
                    try {
                        vpp::Container nested = c.openNested(i);
                        walk(nested, depth + 1);
                        continue;
                    } catch (const std::exception&) {
                        // a leaf, not a nested container
                    }
                }
                if (isCsc) {
                    vpp::ByteView r = c.rawEntryBytes(i);
                    take(e.name, r.data(), r.size());
                }
            } else if (isCsc) {
                auto r = c.decompressEntry(i);
                const bool usable = r.status == vpp::DecodeStatus::Ok ||
                                    r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
                                    r.status == vpp::DecodeStatus::ContentValidated ||
                                    r.status == vpp::DecodeStatus::RecoveredSharedStream ||
                                    r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
                if (usable) {
                    take(e.name, r.data.data(), r.data.size());
                } else {
                    ++g_unreadableCscNamed;
                    g_unreadableNames.push_back(g_currentArchive + ":" + e.name);
                }
            }
        } catch (const std::exception&) {
            if (isCsc) {
                ++g_unreadableCscNamed;
                g_unreadableNames.push_back(g_currentArchive + ":" + e.name + " (exception)");
            }
        }
    }
}

// ------------------------------------------------- independent .csc parse --
// Spec Sec3-Sec5: u16 version(=4), u16 shots, u32 record-array offset, u32
// pad; shot record 0x68 = f32 start, f32 end, 8 x {u32 count, u32 values
// offset, u32 times offset}; value widths 12/8/4/4/4/4/4/4; times f32.

constexpr size_t kW[8] = {12, 8, 4, 4, 4, 4, 4, 4};

struct Track {
    uint32_t n = 0;
    uint32_t valOff = 0, timeOff = 0;
    std::vector<float> t;
    std::vector<uint8_t> v; // n * kW[ch] bytes
};
struct Shot {
    float start = 0, end = 0;
    Track ch[8];
};
struct File {
    uint64_t hash = 0;
    std::string name;
    size_t size = 0;
    uint32_t recOff = 0;
    std::vector<Shot> shots;
    std::vector<std::array<uint32_t, 3>> arrays; // (offset, length, tag = channel*2 + isTimes) of every non-empty array
};

uint16_t rd16(const std::vector<uint8_t>& b, size_t o) {
    uint16_t v = 0;
    std::memcpy(&v, &b[o], 2);
    return v;
}
uint32_t rd32(const std::vector<uint8_t>& b, size_t o) {
    uint32_t v = 0;
    std::memcpy(&v, &b[o], 4);
    return v;
}
float rdf(const std::vector<uint8_t>& b, size_t o) {
    float v = 0;
    std::memcpy(&v, &b[o], 4);
    return v;
}
int16_t rds16(const uint8_t* p) {
    int16_t v = 0;
    std::memcpy(&v, p, 2);
    return v;
}

bool parseIndep(const std::vector<uint8_t>& b, File& f, std::string& why) {
    if (b.size() < 12) { why = "shorter than the 12-byte header"; return false; }
    if (rd16(b, 0) != 4) { why = "version != 4"; return false; }
    const uint32_t count = rd16(b, 2);
    const uint32_t rec = rd32(b, 4);
    if (rec == 0xFFFFFFFFu) { why = "null record offset"; return false; }
    if (static_cast<uint64_t>(rec) + static_cast<uint64_t>(count) * 0x68 != b.size()) {
        why = "record array does not end at EOF";
        return false;
    }
    f.recOff = rec;
    f.size = b.size();
    for (uint32_t s = 0; s < count; ++s) {
        const size_t at = rec + static_cast<size_t>(s) * 0x68;
        Shot sh;
        sh.start = rdf(b, at);
        sh.end = rdf(b, at + 4);
        for (size_t ch = 0; ch < 8; ++ch) {
            const size_t c = at + 8 + ch * 12;
            Track& tr = sh.ch[ch];
            tr.n = rd32(b, c);
            tr.valOff = rd32(b, c + 4);
            tr.timeOff = rd32(b, c + 8);
            if (tr.n == 0) continue;
            const uint64_t tb = static_cast<uint64_t>(tr.n) * 4, vb = static_cast<uint64_t>(tr.n) * kW[ch];
            if (tr.timeOff == 0xFFFFFFFFu || tr.valOff == 0xFFFFFFFFu ||
                tr.timeOff + tb > b.size() || tr.valOff + vb > b.size()) {
                why = "array out of bounds";
                return false;
            }
            tr.t.resize(tr.n);
            for (uint32_t k = 0; k < tr.n; ++k) tr.t[k] = rdf(b, tr.timeOff + static_cast<size_t>(k) * 4);
            tr.v.assign(b.begin() + tr.valOff, b.begin() + tr.valOff + static_cast<ptrdiff_t>(vb));
            f.arrays.push_back({tr.timeOff, static_cast<uint32_t>(tb), static_cast<uint32_t>(ch * 2 + 1)});
            f.arrays.push_back({tr.valOff, static_cast<uint32_t>(vb), static_cast<uint32_t>(ch * 2)});
        }
        f.shots.push_back(std::move(sh));
    }
    return true;
}

// ------------------------------------------------------------ statistics --

double pct(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(p * static_cast<double>(v.size() - 1) + 0.5)];
}

struct Range {
    double mn = 1e300, mx = -1e300;
    std::vector<double> all;
    void add(double x) {
        mn = std::min(mn, x);
        mx = std::max(mx, x);
        all.push_back(x);
    }
    void print(const char* label) const {
        printf("  %-22s n=%-6zu min %.6g  median %.6g  max %.6g\n", label, all.size(), mn, pct(all, 0.5), mx);
    }
};

struct Quat4 { double x, y, z, w; };

Quat4 qFromRaw(const int16_t r[4], double scale, const int perm[4]) {
    return {r[perm[0]] * scale, r[perm[1]] * scale, r[perm[2]] * scale, r[perm[3]] * scale};
}
double qNorm(const Quat4& q) { return std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w); }

// Spec Sec10.4 row-major 3x3.
void qMat(const Quat4& q, double m[3][3]) {
    const double x = q.x, y = q.y, z = q.z, w = q.w;
    m[0][0] = 1 - 2 * (y * y + z * z); m[0][1] = 2 * (x * y - w * z); m[0][2] = 2 * (x * z + w * y);
    m[1][0] = 2 * (x * y + w * z); m[1][1] = 1 - 2 * (x * x + z * z); m[1][2] = 2 * (y * z - w * x);
    m[2][0] = 2 * (x * z - w * y); m[2][1] = 2 * (y * z + w * x); m[2][2] = 1 - 2 * (x * x + y * y);
}

uint64_t g_rng = 0x9E3779B97F4A7C15ull;
uint64_t rnd() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

// ------------------------------------------- independent sampler (Sec10.3) --
// Deliberately structured differently from the library (std::upper_bound
// binary search, not a linear scan; the spec says the two are equal).

struct SampledIndep {
    double pos[3] = {0, 0, 0};
    double q[4] = {0, 0, 0, 1};
    double fov = 0;
    bool dof = false;
    double dofv[5] = {0, 0, 0, 0, 0};
};

// returns: -1 empty, 0 single(a), 1 blend(a,b,f)
int locateIndep(const Track& tr, double t, size_t& a, size_t& b, double& f) {
    const size_t n = tr.t.size();
    if (n == 0) return -1;
    if (n == 1 || t <= tr.t[0]) { a = 0; return 0; }
    if (t >= tr.t[n - 1]) { a = n - 1; return 0; }
    // first index with T[i] > t
    // first index with T[i] > t (t strictly between T[0] and T[n-1] here,
    // and every shipped time track is strictly ascending, Sec1)
    size_t i = static_cast<size_t>(std::upper_bound(tr.t.begin(), tr.t.end(), t,
                                                    [](double tv, float kv) { return tv < static_cast<double>(kv); }) -
                                   tr.t.begin());
    if (i < 1) i = 1;
    if (i > n - 1) i = n - 1;
    const double d = static_cast<double>(tr.t[i]) - static_cast<double>(tr.t[i - 1]);
    if (d <= 0) { a = i; return 0; }
    a = i - 1;
    b = i;
    f = (t - static_cast<double>(tr.t[i - 1])) / d;
    return 1;
}

double fAt(const Track& tr, size_t k, size_t comp, size_t width) {
    float v = 0;
    std::memcpy(&v, tr.v.data() + k * width + comp * 4, 4);
    return v;
}

void slerpIndep(const double a[4], const double bIn[4], double f, double out[4]) {
    double b[4] = {bIn[0], bIn[1], bIn[2], bIn[3]};
    double c = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (c < 0) { for (double& x : b) x = -x; c = -c; }
    double w0 = 1 - f, w1 = f;
    if (1 - c > 1e-7) {
        const double th = std::acos(c);
        w0 = std::sin((1 - f) * th) / std::sin(th);
        w1 = std::sin(f * th) / std::sin(th);
    }
    for (int i = 0; i < 4; ++i) out[i] = w0 * a[i] + w1 * b[i];
}

void keyQuat(const Track& tr, size_t k, double q[4]) {
    const double S = 16385.0 / 268435456.0;
    for (int c = 0; c < 4; ++c) q[c] = rds16(tr.v.data() + k * 8 + c * 2) * S;
}

double scalarIndep(const Track& tr, double t) {
    size_t a = 0, b = 0;
    double f = 0;
    const int k = locateIndep(tr, t, a, b, f);
    if (k < 0) return 0;
    if (k == 0) return fAt(tr, a, 0, 4);
    return (1 - f) * fAt(tr, a, 0, 4) + f * fAt(tr, b, 0, 4);
}

SampledIndep sampleIndep(const Shot& s, double t) {
    SampledIndep o;
    {
        size_t a = 0, b = 0;
        double f = 0;
        const int k = locateIndep(s.ch[0], t, a, b, f);
        for (size_t c = 0; c < 3 && k >= 0; ++c) {
            o.pos[c] = k == 0 ? fAt(s.ch[0], a, c, 12)
                              : (1 - f) * fAt(s.ch[0], a, c, 12) + f * fAt(s.ch[0], b, c, 12);
        }
    }
    {
        size_t a = 0, b = 0;
        double f = 0;
        const int k = locateIndep(s.ch[1], t, a, b, f);
        if (k == 0) keyQuat(s.ch[1], a, o.q);
        if (k == 1) {
            double q0[4], q1[4];
            keyQuat(s.ch[1], a, q0);
            keyQuat(s.ch[1], b, q1);
            slerpIndep(q0, q1, f, o.q);
        }
    }
    o.fov = scalarIndep(s.ch[2], t);
    o.dof = s.ch[3].n != 0;
    if (o.dof) for (size_t c = 0; c < 5; ++c) o.dofv[c] = scalarIndep(s.ch[3 + c], t);
    return o;
}

const char* verdict(bool ok) { return ok ? "PASS" : "FAIL"; }

} // namespace

int main(int argc, char** argv) {
    // ---------------------------------------------------------------- walk --
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> bytes = readFile(argv[i]);
        if (bytes.empty()) { printf("could not read %s\n", argv[i]); continue; }
        g_currentArchive = argv[i];
        const size_t before = g_entries.size();
        try {
            vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
            walk(c, 0);
        } catch (const std::exception& ex) {
            printf("skip %s: %s\n", argv[i], ex.what());
            continue;
        }
        printf("scanned %-58s .csc_pc entries: %zu\n", argv[i], g_entries.size() - before);
        fflush(stdout);
    }

    printf("\n================ CORPUS ================\n");
    printf("directory entries visited (all archives)  : %lld\n", g_entriesSeen);
    printf(".csc_pc entries read                      : %zu\n", g_entries.size());
    printf(".csc_pc-named entries the container could not decode : %d\n", g_unreadableCscNamed);
    for (const auto& n : g_unreadableNames) printf("    unreadable: %s\n", n.c_str());
    printf("distinct contents (by FNV-1a64+size)      : %zu\n", g_distinct.size());
    printf("distinct names (case-folded)              : %zu\n", g_hashesByName.size());
    int nameConflicts = 0;
    for (const auto& kv : g_hashesByName) if (kv.second.size() > 1) ++nameConflicts;
    printf("names with >1 distinct content            : %d (spec Sec2: 0, duplicates byte-identical)\n", nameConflicts);
    {
        std::map<std::string, int> byArchive;
        for (const auto& e : g_entries) ++byArchive[e.archive.substr(e.archive.find_last_of("/\\") + 1)];
        for (const auto& kv : byArchive) printf("    %-28s %d entries\n", kv.first.c_str(), kv.second);
    }

    // --------------------------------------------- independent structure --
    std::vector<File> files;
    int parseFail = 0;
    for (const auto& kv : g_distinct) {
        File f;
        f.hash = kv.first;
        std::string why;
        if (!parseIndep(kv.second, f, why)) {
            ++parseFail;
            printf("  independent parse FAILED (hash %016llx, %zu bytes): %s\n",
                   static_cast<unsigned long long>(kv.first), kv.second.size(), why.c_str());
            continue;
        }
        for (const auto& e : g_entries) if (e.hash == kv.first) { f.name = e.name; break; }
        files.push_back(std::move(f));
    }
    printf("\n================ STRUCTURE (independent parse, spec Sec3-Sec5) ================\n");
    printf("distinct files parsed OK (version 4, exact-size, all arrays in bounds): %zu / %zu\n",
           files.size(), g_distinct.size());

    long long nShots = 0, nTracksAll = 0, nTracksNonEmpty = 0, nSingle = 0, nMulti = 0, nAsc = 0;
    long long firstKeyIsStartExact = 0, firstKeyIsStart1ms = 0, tracksWithKeys = 0;
    long long keysTotal = 0, keysInside = 0;
    double maxOver = 0;
    long long contigPairs = 0, contig2e4 = 0, contig1ms = 0, firstZero = 0;
    double worstGap = 0;

    long long dofAll = 0, dofNone = 0, dofPartial = 0;
    long long ch0TailGt1f = 0, ch0Shots = 0;
    int minShots = 1 << 30, maxShots = 0;
    long long chKeys[8] = {0}, chPresent[8] = {0};
    for (const auto& f : files) {
        minShots = std::min<int>(minShots, static_cast<int>(f.shots.size()));
        maxShots = std::max<int>(maxShots, static_cast<int>(f.shots.size()));
        if (!f.shots.empty() && f.shots[0].start == 0.0f) ++firstZero;
        for (size_t s = 0; s < f.shots.size(); ++s) {
            const Shot& sh = f.shots[s];
            ++nShots;
            if (s > 0) {
                ++contigPairs;
                const double gap = std::fabs(static_cast<double>(sh.start) - static_cast<double>(f.shots[s - 1].end));
                worstGap = std::max(worstGap, gap);
                if (gap <= 2e-4) ++contig2e4;
                if (gap <= 1e-3) ++contig1ms;
            }
            int present = 0;
            for (int ch = 0; ch < 8; ++ch) {
                const Track& tr = sh.ch[ch];
                ++nTracksAll;
                chKeys[ch] += tr.n;
                if (tr.n) { ++chPresent[ch]; ++nTracksNonEmpty; }
                if (ch >= 3 && tr.n) ++present;
                if (tr.n == 1) ++nSingle;
                if (tr.n > 1) {
                    ++nMulti;
                    bool asc = true;
                    for (size_t k = 1; k < tr.t.size(); ++k) if (!(tr.t[k] > tr.t[k - 1])) asc = false;
                    if (asc) ++nAsc;
                }
                if (tr.n) {
                    ++tracksWithKeys;
                    if (tr.t[0] == sh.start) ++firstKeyIsStartExact;
                    if (std::fabs(static_cast<double>(tr.t[0]) - sh.start) <= 1e-3) ++firstKeyIsStart1ms;
                    for (float tv : tr.t) {
                        ++keysTotal;
                        const double over = std::max(sh.start - static_cast<double>(tv), static_cast<double>(tv) - sh.end);
                        if (over <= 2e-4) ++keysInside;
                        maxOver = std::max(maxOver, over);
                    }
                }
            }
            // DOF group all-or-nothing: identical key counts on channels 3..7
            bool same = true;
            for (int ch = 4; ch < 8; ++ch) if (sh.ch[ch].n != sh.ch[3].n) same = false;
            if (present == 5 && same) ++dofAll; else if (present == 0) ++dofNone; else ++dofPartial;
            ++ch0Shots;
            if (!sh.ch[0].t.empty() && static_cast<double>(sh.end) - sh.ch[0].t.back() > 1.0 / 30.0) ++ch0TailGt1f;
        }
    }
    printf("shots: %lld total (spec 1,405), per file %d..%d (spec 1..82)\n", nShots, minShots, maxShots);
    printf("first shot starts at exactly 0.0: %lld / %zu files (spec 96/96)\n", firstZero, files.size());
    printf("consecutive shots tile: gap<=2e-4 s %lld / %lld, gap<=1e-3 s %lld / %lld, worst gap %.3g s (spec 1,309/1,309, worst 1.07e-4)\n",
           contig2e4, contigPairs, contig1ms, contigPairs, worstGap);
    printf("tracks: %lld total, %lld non-empty (spec 9,260), %lld single-key (spec 7,261), %lld multi-key (spec 1,999)\n",
           nTracksAll, nTracksNonEmpty, nSingle, nMulti);
    printf("multi-key time tracks strictly ascending: %lld / %lld (spec 1,999/1,999)\n", nAsc, nMulti);
    printf("ABSOLUTE times: first key == shot start bit-exactly: %lld / %lld tracks; within 1 ms: %lld / %lld (spec 9,260/9,260)\n",
           firstKeyIsStartExact, tracksWithKeys, firstKeyIsStart1ms, tracksWithKeys);
    printf("   control (shot-relative reading: first key == 0): %lld / %lld\n", [&] {
        long long n = 0;
        for (const auto& f : files) for (const auto& sh : f.shots) for (int ch = 0; ch < 8; ++ch)
            if (sh.ch[ch].n && sh.ch[ch].t[0] == 0.0f) ++n;
        return n;
    }(), tracksWithKeys);
    printf("all keys inside [start,end] (2e-4 s slack): %lld / %lld, worst overrun %.3g s (spec: all 68,736)\n",
           keysInside, keysTotal, maxOver);
    printf("channel 0 last key more than 1/30 s before shot end: %lld / %lld shots (spec 927/1,405)\n", ch0TailGt1f, ch0Shots);
    printf("DOF group ch3-7: all five with identical counts %lld, none %lld, partial/mismatched %lld (spec: 1,009 / 396 / 0)\n",
           dofAll, dofNone, dofPartial);
    // Sec10.6.3 tail-gap numbers, measured several ways so a definitional mismatch shows.
    {
        std::vector<double> tails, tailsPos;
        long long gt[6] = {0}, neg = 0;
        const double th[6] = {1.0 / 30.0, 0.0335, 0.05, 2.0 / 30.0, 0.1, 0.5};
        long long anyChGt1f = 0, anyN = 0;
        for (const auto& f : files) for (const auto& sh : f.shots) {
            if (!sh.ch[0].n) continue;
            const double tail = static_cast<double>(sh.end) - sh.ch[0].t.back();
            tails.push_back(tail);
            if (tail < 0) ++neg;
            if (tail > 1.0 / 30.0) tailsPos.push_back(tail);
            for (int i = 0; i < 6; ++i) if (tail > th[i]) ++gt[i];
            double lastAny = -1e300;
            for (int ch = 0; ch < 8; ++ch) if (sh.ch[ch].n) lastAny = std::max<double>(lastAny, sh.ch[ch].t.back());
            ++anyN;
            if (static_cast<double>(sh.end) - lastAny > 1.0 / 30.0) ++anyChGt1f;
        }
        printf("tail (shot end - ch0 last key), %zu shots: > 1/30 s: %lld, > 0.0335: %lld, > 0.05: %lld, > 2/30: %lld, > 0.1: %lld, > 0.5: %lld; negative: %lld (spec Sec10.6.3: 927 = 66.0%% 'more than one frame')\n",
               tails.size(), gt[0], gt[1], gt[2], gt[3], gt[4], gt[5], neg);
        printf("   among tails > 1/30 s: median %.3f p90 %.3f max %.3f (spec: median 1.267, p90 4.10, max 81.23); over ALL shots: median %.3f p90 %.3f\n",
               pct(tailsPos, 0.5), pct(tailsPos, 0.9), pct(tailsPos, 1.0), pct(tails, 0.5), pct(tails, 0.9));
        printf("   shots within 1/30 s of the end: %lld / %zu (%.1f%%; spec 'only 34%%')   any-channel last key > 1/30 s before end: %lld / %lld\n",
               static_cast<long long>(tails.size()) - gt[0], tails.size(),
               100.0 * static_cast<double>(static_cast<long long>(tails.size()) - gt[0]) / static_cast<double>(tails.size()), anyChGt1f, anyN);
        // files whose last shot end matches the file's latest keyframe (spec Sec4: 39/96)
        int m1 = 0, m2 = 0, m3 = 0;
        for (const auto& f : files) {
            double latest = -1e300, latestCh0 = -1e300;
            for (const auto& sh : f.shots) for (int ch = 0; ch < 8; ++ch) if (sh.ch[ch].n) {
                latest = std::max<double>(latest, sh.ch[ch].t.back());
                if (ch == 0) latestCh0 = std::max<double>(latestCh0, sh.ch[ch].t.back());
            }
            const double e = f.shots.back().end;
            if (std::fabs(e - latest) <= 1.0 / 30.0) ++m1;
            if (std::fabs(e - latest) <= 1e-3) ++m2;
            if (std::fabs(e - latestCh0) <= 1.0 / 30.0) ++m3;
        }
        printf("   files whose last shot end == file's latest keyframe: within 1/30 s %d, within 1 ms %d, ch0 only within 1/30 s %d / %zu (spec Sec4: 39/96)\n",
               m1, m2, m3, files.size());
        {
            std::vector<double> sorted = tails;
            std::sort(sorted.begin(), sorted.end(), [](double a, double b) { return a > b; });
            if (sorted.size() > 928) {
                printf("   the 927th / 928th largest tails are %.5f s / %.5f s (= %.3f / %.3f frames): a threshold in that gap reproduces the spec's 927\n",
                       sorted[926], sorted[927], sorted[926] * 30.0, sorted[927] * 30.0);
            }
        }
        long long fb[9] = {0};
        for (double tl : tails) {
            const double fr = tl * 30.0;
            fb[fr < 1 ? 0 : fr < 2 ? 1 : fr < 3 ? 2 : fr < 4 ? 3 : fr < 5 ? 4 : fr < 6 ? 5 : fr < 10 ? 6 : fr < 30 ? 7 : 8]++;
        }
        printf("   tail histogram in 1/30 s frames: [0,1) %lld [1,2) %lld [2,3) %lld [3,4) %lld [4,5) %lld [5,6) %lld [6,10) %lld [10,30) %lld [30,inf) %lld\n",
               fb[0], fb[1], fb[2], fb[3], fb[4], fb[5], fb[6], fb[7], fb[8]);
        const double tol[6] = {1.0 / 30.0, 0.0335, 0.05, 2.0 / 30.0, 3.0 / 30.0, 0.5};
        long long fm[6] = {0};
        for (const auto& f : files) {
            double latest = -1e300;
            for (const auto& sh : f.shots) for (int ch = 0; ch < 8; ++ch) if (sh.ch[ch].n) latest = std::max<double>(latest, sh.ch[ch].t.back());
            for (int i = 0; i < 6; ++i) if (std::fabs(static_cast<double>(f.shots.back().end) - latest) <= tol[i]) ++fm[i];
        }
        printf("   files with |last end - latest key| <= 1/30, 0.0335, 0.05, 2/30, 3/30, 0.5 s: %lld %lld %lld %lld %lld %lld of %zu\n",
               fm[0], fm[1], fm[2], fm[3], fm[4], fm[5], files.size());
    }
    printf("keys per channel:");
    for (int ch = 0; ch < 8; ++ch) printf("  ch%d %lld (%lld shots)", ch, chKeys[ch], chPresent[ch]);
    printf("\n");
    // Packing / width check. Spec Sec3/Sec8: the keyframe region is "packed back
    // to back with no padding". Collapse arrays with an identical (offset,length)
    // to one, then test whether the unique arrays tile [start, record_offset)
    // with no gap and no partial overlap, for start = 0x0C (the spec's stated
    // region start) and start = 0x08 (what the data suggests).
    {
        int tiled12 = 0, tiled8 = 0, filesWithShared = 0, firstAt8 = 0, firstAt12 = 0, wordAt8Zero = 0, firstIsTimes = 0;
        long long refs = 0, uniq = 0;
        std::map<std::string, long long> sharePairs, firstTags;
        for (auto& f : files) {
            auto a = f.arrays;
            refs += static_cast<long long>(a.size());
            std::map<std::pair<uint32_t, uint32_t>, std::set<uint32_t>> byIv;
            std::map<std::pair<uint32_t, uint32_t>, int> refCount;
            for (const auto& iv : a) { byIv[{iv[0], iv[1]}].insert(iv[2]); ++refCount[{iv[0], iv[1]}]; }
            uniq += static_cast<long long>(byIv.size());
            bool shared = false;
            for (const auto& kv : byIv) {
                if (refCount[kv.first] > 1) {
                    shared = true;
                    std::string key;
                    for (uint32_t tg : kv.second) key += (key.empty() ? "" : "+") + std::string(tg & 1 ? "t" : "v") + std::to_string(tg / 2);
                    ++sharePairs[key];
                }
            }
            if (shared) ++filesWithShared;
            for (const uint64_t start : {uint64_t(12), uint64_t(8)}) {
                uint64_t cursor = start;
                bool ok = true;
                for (const auto& kv : byIv) {
                    const uint64_t off = kv.first.first, end = off + kv.first.second;
                    if (off != cursor) ok = false; // a gap (off > cursor) or a partial overlap (off < cursor)
                    cursor = std::max<uint64_t>(cursor, end);
                }
                if (cursor != f.recOff) ok = false;
                if (ok) (start == 12 ? tiled12 : tiled8)++;
            }
            const auto& first = *byIv.begin();
            if (first.first.first == 8) ++firstAt8;
            if (first.first.first == 12) ++firstAt12;
            std::string key;
            for (uint32_t tg : first.second) key += (key.empty() ? "" : "+") + std::string(tg & 1 ? "t" : "v") + std::to_string(tg / 2);
            ++firstTags[key + " @" + std::to_string(first.first.first)];
            if (first.second.size() && (*first.second.begin() & 1)) ++firstIsTimes;
            if (rd32(g_distinct.at(f.hash), 8) == 0) ++wordAt8Zero;
        }
        printf("packing (unique arrays; widths 12,8,4x6): tile [0x0C, record_offset) with no gap/overlap: %d / %zu files ; tile [0x08, record_offset): %d / %zu files\n",
               tiled12, files.size(), tiled8, files.size());
        printf("   lowest-addressed array starts at +0x08 in %d files, at +0x0C in %d files ; it is a TIMES array in %d files ; u32 at +0x08 == 0 in %d files\n",
               firstAt8, firstAt12, firstIsTimes, wordAt8Zero);
        for (const auto& kv : firstTags) printf("      lowest array = %-14s in %lld files\n", kv.first.c_str(), kv.second);
        printf("   array references %lld, unique (offset,length) intervals %lld -> %lld references share an interval with another (in %d files)\n",
               refs, uniq, refs - uniq, filesWithShared);
        printf("   (spec Sec3/Sec8 do not mention shared arrays.) Sharing groups (t=times array, v=values array, N=channel):\n");
        for (const auto& kv : sharePairs) printf("      %-28s %lld\n", kv.first.c_str(), kv.second);
    }

    // ------------------------------------------------------- channel 1 ----
    const double S = 16385.0 / 268435456.0;
    const double TOL = 2.0 * S; // 2 LSB: theoretical truncation bound (each component off by < 1 LSB)
    printf("\n================ CHANNEL 1: 4 x s16 (x,y,z,w) * 16385/2^28 ================\n");
    printf("scale S = %.10e ; unit-norm tolerance TOL = 2*S = %.6e (each component quantised to < 1 LSB -> |norm-1| <= 2 LSB)\n", S, TOL);

    std::vector<std::array<int16_t, 4>> rawAll;
    std::vector<std::vector<size_t>> ch1Runs; // indices into rawAll per track, for continuity
    for (const auto& f : files) for (const auto& sh : f.shots) {
        const Track& tr = sh.ch[1];
        std::vector<size_t> run;
        for (size_t k = 0; k < tr.n; ++k) {
            std::array<int16_t, 4> r{};
            for (int c = 0; c < 4; ++c) r[static_cast<size_t>(c)] = rds16(tr.v.data() + k * 8 + static_cast<size_t>(c) * 2);
            run.push_back(rawAll.size());
            rawAll.push_back(r);
        }
        if (!run.empty()) ch1Runs.push_back(run);
    }
    const size_t N = rawAll.size();
    printf("channel-1 keyframes = quaternions decoded: %zu across %zu distinct files, %lld shots (spec: 31,898 keys / 1,405 shots)\n",
           N, files.size(), chPresent[1]);

    static const int idPerm[4] = {0, 1, 2, 3};
    struct Row { const char* name; long long total, within; double med, mn, mx; };
    auto evalScale = [&](const char* name, double sc) {
        Row r{name, static_cast<long long>(N), 0, 0, 1e300, -1e300};
        std::vector<double> dev;
        dev.reserve(N);
        for (const auto& q : rawAll) {
            int16_t rr[4] = {q[0], q[1], q[2], q[3]};
            const double d = qNorm(qFromRaw(rr, sc, idPerm)) - 1.0;
            dev.push_back(d);
            if (std::fabs(d) <= TOL) ++r.within;
            r.mn = std::min(r.mn, d);
            r.mx = std::max(r.mx, d);
        }
        r.med = pct(dev, 0.5);
        return r;
    };
    auto printRow = [&](const Row& r) {
        printf("  %-46s within TOL %6lld / %-6lld (%6.2f%%)  median(norm-1) %+.3e  range [%+.4g, %+.4g]\n",
               r.name, r.within, r.total, 100.0 * static_cast<double>(r.within) / static_cast<double>(std::max<long long>(1, r.total)),
               r.med, r.mn, r.mx);
    };

    const Row spec = evalScale("SPEC scale 16385/2^28", S);
    printf("\nGate (tolerance TOL):\n");
    printRow(spec);

    // distribution of |norm-1|
    {
        std::vector<double> ad;
        ad.reserve(N);
        for (const auto& q : rawAll) {
            int16_t rr[4] = {q[0], q[1], q[2], q[3]};
            ad.push_back(std::fabs(qNorm(qFromRaw(rr, S, idPerm)) - 1.0));
        }
        printf("distribution of |norm-1| (absolute; in units of S = 1 LSB in brackets):\n");
        const double ps[] = {0.0, 0.25, 0.5, 0.75, 0.9, 0.99, 0.999, 1.0};
        for (double p : ps) printf("    p%-6.1f %.3e  [%.3f LSB]\n", p * 100, pct(ad, p), pct(ad, p) / S);
        long long b[7] = {0};
        for (double d : ad) {
            const double u = d / S;
            b[u < 0.25 ? 0 : u < 0.5 ? 1 : u < 1 ? 2 : u < 2 ? 3 : u < 4 ? 4 : u < 8 ? 5 : 6]++;
        }
        printf("    histogram in LSB: [0,.25) %lld  [.25,.5) %lld  [.5,1) %lld  [1,2) %lld  [2,4) %lld  [4,8) %lld  [8,inf) %lld\n",
               b[0], b[1], b[2], b[3], b[4], b[5], b[6]);
        long long w1 = 0, w1e3 = 0, w1e2 = 0, w10 = 0;
        for (double d : ad) { if (d <= S) ++w1; if (d <= 1e-3) ++w1e3; if (d <= 1e-2) ++w1e2; if (d <= 0.1) ++w10; }
        printf("    within 1 LSB %lld, within 1e-3 %lld, within 1e-2 %lld, within 10%% %lld  (of %zu)\n", w1, w1e3, w1e2, w10, N);
        // scale implied by the data alone: 1 / median(|raw|)
        std::vector<double> rn;
        for (const auto& q : rawAll) {
            const double a = q[0], bq = q[1], c = q[2], d = q[3];
            rn.push_back(std::sqrt(a * a + bq * bq + c * c + d * d));
        }
        const double medRaw = pct(rn, 0.5);
        printf("    data-implied scale = 1/median(|raw|) = %.10e ; ratio to spec scale = %.7f (median |raw| = %.2f, 1/S = %.2f)\n",
               1.0 / medRaw, (1.0 / medRaw) / S, medRaw, 1.0 / S);
    }

    printf("\nControls that must FAIL (same TOL, same %zu quaternions):\n", N);
    const Row c1 = evalScale("scale 1/32767 (the old refuted reading)", 1.0 / 32767.0);
    const Row c2 = evalScale("scale 1/32768 = 2^-15", 1.0 / 32768.0);
    const Row c3 = evalScale("scale 2^-14 (pin: fails median test only)", 1.0 / 16384.0);
    const Row c4 = evalScale("scale 2/32767 (pin: fails median test only)", 2.0 / 32767.0);
    const Row c5 = evalScale("scale 1/16383 (= spec within 4e-9; NOT a control)", 1.0 / 16383.0);
    printRow(c1); printRow(c2); printRow(c3); printRow(c4); printRow(c5);
    printf("  scale pin: |median(norm-1)| < 2e-6 ?  spec %s (%+.2e) | 2^-14 %s (%+.2e) | 2/32767 %s (%+.2e) | 1/16383 %s (%+.2e)\n",
           verdict(std::fabs(spec.med) < 2e-6), spec.med, verdict(std::fabs(c3.med) < 2e-6), c3.med,
           verdict(std::fabs(c4.med) < 2e-6), c4.med, verdict(std::fabs(c5.med) < 2e-6), c5.med);
    {
        // unsigned / byte-swapped / random / channel-0-bytes / misaligned
        Row ru{"s16 read as UNSIGNED u16", static_cast<long long>(N), 0, 0, 0, 0};
        Row rb{"big-endian s16 (bytes swapped)", static_cast<long long>(N), 0, 0, 0, 0};
        for (const auto& q : rawAll) {
            double su = 0, sb = 0;
            for (int c = 0; c < 4; ++c) {
                const uint16_t u = static_cast<uint16_t>(q[static_cast<size_t>(c)]);
                su += static_cast<double>(u) * u;
                const uint16_t sw = static_cast<uint16_t>((u >> 8) | (u << 8));
                const double sv = static_cast<int16_t>(sw);
                sb += sv * sv;
            }
            if (std::fabs(std::sqrt(su) * S - 1) <= TOL) ++ru.within;
            if (std::fabs(std::sqrt(sb) * S - 1) <= TOL) ++rb.within;
        }
        Row rr{"random s16 quads (deterministic PRNG)", static_cast<long long>(N), 0, 0, 0, 0};
        for (size_t i = 0; i < N; ++i) {
            double s2 = 0;
            for (int c = 0; c < 4; ++c) { const double v = static_cast<int16_t>(rnd() & 0xFFFF); s2 += v * v; }
            if (std::fabs(std::sqrt(s2) * S - 1) <= TOL) ++rr.within;
        }
        Row r0{"channel 0's bytes read as s16 quads", 0, 0, 0, 0, 0};
        Row rm{"channel 1 read misaligned by +2 bytes (WEAK)", 0, 0, 0, 0, 0};
        for (const auto& f : files) for (const auto& sh : f.shots) {
            const Track& t0 = sh.ch[0];
            for (size_t k = 0; k + 8 <= t0.v.size(); k += 8) {
                double s2 = 0;
                for (int c = 0; c < 4; ++c) { const double v = rds16(t0.v.data() + k + static_cast<size_t>(c) * 2); s2 += v * v; }
                ++r0.total;
                if (std::fabs(std::sqrt(s2) * S - 1) <= TOL) ++r0.within;
            }
            const Track& t1 = sh.ch[1];
            for (size_t k = 0; k + 1 < t1.n; ++k) {
                double s2 = 0;
                for (int c = 0; c < 4; ++c) { const double v = rds16(t1.v.data() + k * 8 + 2 + static_cast<size_t>(c) * 2); s2 += v * v; }
                ++rm.total;
                if (std::fabs(std::sqrt(s2) * S - 1) <= TOL) ++rm.within;
            }
        }
        for (Row* r : {&ru, &rb, &rr, &r0, &rm}) {
            printf("  %-46s within TOL %6lld / %-6lld (%6.2f%%)\n", r->name, r->within, r->total,
                   100.0 * static_cast<double>(r->within) / static_cast<double>(std::max<long long>(1, r->total)));
        }
    }

    // component ORDER: norm is permutation-invariant, so the unit-norm gate cannot see it.
    printf("\nComponent order. Unit-norm is INVARIANT under any permutation of (x,y,z,w) (a norm sums the four squares),\n"
           "so the unit-norm gate is NOT a control for order; measured, all 24 orders give the same count:\n");
    {
        int perm[4] = {0, 1, 2, 3};
        long long mn = 1LL << 60, mx = -1;
        struct Al { int p[4]; double frac; };
        std::vector<Al> als;
        do {
            long long within = 0, aligned[3][3] = {{0}};
            for (const auto& q : rawAll) {
                int16_t rr[4] = {q[0], q[1], q[2], q[3]};
                const Quat4 qq = qFromRaw(rr, S, perm);
                if (std::fabs(qNorm(qq) - 1) <= TOL) ++within;
                double m[3][3];
                qMat(qq, m);
                for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) if (i != j && std::fabs(m[i][j]) < 0.02) ++aligned[i][j];
            }
            mn = std::min(mn, within);
            mx = std::max(mx, within);
            long long best = 0;
            for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) best = std::max(best, aligned[i][j]);
            Al a;
            for (int i = 0; i < 4; ++i) a.p[i] = perm[i];
            a.frac = static_cast<double>(best) / static_cast<double>(N);
            als.push_back(a);
        } while (std::next_permutation(perm, perm + 4));
        printf("  unit-norm within TOL across 24 orders: min %lld, max %lld (of %zu)\n", mn, mx, N);
        std::sort(als.begin(), als.end(), [](const Al& a, const Al& b) { return a.frac > b.frac; });
        printf("  SUPPLEMENTARY order probe (NOT from the spec; a plausibility prior): a roll-free camera makes one OFF-diagonal entry of the\n"
               "  Sec10.4 matrix M ~ 0. For each order take the off-diagonal cell (i!=j) with the most keys having |M[i][j]| < 0.02 (~1.1 deg),\n"
               "  order p means (x,y,z,w) = (raw[p0],raw[p1],raw[p2],raw[p3]). (A first probe on diagonal entries was degenerate: identical for all 24 orders.)\n");
        if (als.front().frac == als.back().frac) {
            printf("    RESULT: all 24 orders give the identical best-cell fraction %.2f%% - the probe cannot tell orders apart\n"
                   "    (the multiset of |M[i][j]| is permutation-invariant: every entry is +-(ab +- cd) over the 3 pairings of the 4 components).\n"
                   "    So the component ORDER cannot be tested from the data without a prior on which world axis is up / handedness (spec Sec10.9 item 1);\n"
                   "    the (x,y,z,w) order rests on the spec's disassembly claim alone.\n", 100.0 * als.front().frac);
        } else {
            for (const auto& a : als) {
                printf("    order (%d,%d,%d,%d)%s  best roll-free cell %6.2f%%  [w slot = raw[%d]]\n", a.p[0], a.p[1], a.p[2], a.p[3],
                       (a.p[0] == 0 && a.p[1] == 1 && a.p[2] == 2 && a.p[3] == 3) ? " <- spec (x,y,z,w)" : "                   ",
                       100.0 * a.frac, a.p[3]);
            }
        }
    }

    // per-slot statistics: which slot behaves like a scalar part?
    {
        long long nonneg[4] = {0}, largest[4] = {0}, signChange[4] = {0}, pairs = 0, dotNeg = 0;
        double meanAbs[4] = {0};
        for (const auto& q : rawAll) {
            int arg = 0;
            for (size_t c = 0; c < 4; ++c) {
                if (q[c] >= 0) ++nonneg[c];
                meanAbs[c] += std::fabs(static_cast<double>(q[c])) * S;
                if (std::abs(q[c]) > std::abs(q[static_cast<size_t>(arg)])) arg = static_cast<int>(c);
            }
            ++largest[arg];
        }
        for (const auto& run : ch1Runs) for (size_t k = 1; k < run.size(); ++k) {
            ++pairs;
            double d = 0;
            for (size_t c = 0; c < 4; ++c) {
                d += static_cast<double>(rawAll[run[k - 1]][c]) * rawAll[run[k]][c];
                if ((rawAll[run[k - 1]][c] >= 0) != (rawAll[run[k]][c] >= 0)) ++signChange[c];
            }
            if (d < 0) ++dotNeg;
        }
        printf("  per raw slot [0..3]: fraction >= 0: %.4f %.4f %.4f %.4f ; mean |value|: %.4f %.4f %.4f %.4f ; is largest |component|: %.4f %.4f %.4f %.4f\n",
               static_cast<double>(nonneg[0]) / static_cast<double>(N), static_cast<double>(nonneg[1]) / static_cast<double>(N),
               static_cast<double>(nonneg[2]) / static_cast<double>(N), static_cast<double>(nonneg[3]) / static_cast<double>(N),
               meanAbs[0] / static_cast<double>(N), meanAbs[1] / static_cast<double>(N), meanAbs[2] / static_cast<double>(N), meanAbs[3] / static_cast<double>(N),
               static_cast<double>(largest[0]) / static_cast<double>(N), static_cast<double>(largest[1]) / static_cast<double>(N),
               static_cast<double>(largest[2]) / static_cast<double>(N), static_cast<double>(largest[3]) / static_cast<double>(N));
        printf("  consecutive-key pairs %lld: sign change per slot: %lld %lld %lld %lld ; dot < 0 (whole-quaternion sign flip): %lld\n",
               pairs, signChange[0], signChange[1], signChange[2], signChange[3], dotNeg);
        printf("  (a slot that is >= 0 on ~100%% of keys would suggest a w >= 0 canonical hemisphere for that slot; all ~50%% => no such convention is visible)\n");
    }

    // continuity (consecutive keys) vs random pairs
    {
        std::vector<double> adj, rndp;
        auto dot = [&](size_t a, size_t b) {
            double s = 0;
            for (int c = 0; c < 4; ++c) s += static_cast<double>(rawAll[a][static_cast<size_t>(c)]) * rawAll[b][static_cast<size_t>(c)];
            return s * S * S;
        };
        auto angle = [&](double d) { return 2.0 * std::acos(std::min(1.0, std::fabs(d))) * 180.0 / 3.14159265358979323846; };
        for (const auto& run : ch1Runs) for (size_t k = 1; k < run.size(); ++k) adj.push_back(angle(dot(run[k - 1], run[k])));
        for (size_t i = 0; i < adj.size(); ++i) rndp.push_back(angle(dot(rnd() % N, rnd() % N)));
        printf("\nContinuity of consecutive ch1 keys (angle of relative rotation, degrees): n=%zu median %.3f  p99 %.3f  max %.3f\n",
               adj.size(), pct(adj, 0.5), pct(adj, 0.99), pct(adj, 1.0));
        printf("   control, randomly paired keys: median %.3f  p99 %.3f   (spec: 0.61 / 5.46 vs 93.3)\n", pct(rndp, 0.5), pct(rndp, 0.99));
        long long neg = 0, tot = 0, big20 = 0;
        for (const auto& run : ch1Runs) for (size_t k = 1; k < run.size(); ++k) {
            ++tot;
            if (dot(run[k - 1], run[k]) < 0) ++neg;
            if (angle(dot(run[k - 1], run[k])) > 20.0) ++big20;
        }
        printf("   consecutive key pairs with dot < 0 (slerp's shortest-arc negation fires): %lld / %lld ; pairs > 20 deg apart: %lld\n", neg, tot, big20);
    }

    // ------------------------------------------------- value ranges --------
    printf("\n================ VALUE RANGES (distinct files, per key) ================\n");
    {
        Range px, py, pz, fov, dof[5];
        long long fovIn = 0, fovN = 0, finite = 0, posN = 0;
        long long ordOk = 0, ordN = 0, ordAny[24] = {0};
        double minDen = 1e300;
        long long c3pos = 0, c3n = 0;
        for (const auto& f : files) for (const auto& sh : f.shots) {
            const Track& p = sh.ch[0];
            for (size_t k = 0; k < p.n; ++k) {
                const double x = fAt(p, k, 0, 12), y = fAt(p, k, 1, 12), z = fAt(p, k, 2, 12);
                ++posN;
                if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) ++finite;
                px.add(x); py.add(y); pz.add(z);
            }
            const Track& fv = sh.ch[2];
            for (size_t k = 0; k < fv.n; ++k) {
                const double v = fAt(fv, k, 0, 4);
                ++fovN;
                if (v > 0 && v < 3.14159265358979323846) ++fovIn;
                fov.add(v);
            }
            for (int c = 0; c < 5; ++c) {
                const Track& d = sh.ch[3 + c];
                for (size_t k = 0; k < d.n; ++k) dof[c].add(fAt(d, k, 0, 4));
            }
            const Track& d3 = sh.ch[3];
            for (size_t k = 0; k < d3.n && sh.ch[7].n == d3.n; ++k) {
                double v[4] = {fAt(sh.ch[4], k, 0, 4), fAt(sh.ch[5], k, 0, 4), fAt(sh.ch[6], k, 0, 4), fAt(sh.ch[7], k, 0, 4)};
                ++ordN;
                if (v[0] <= v[1] && v[1] <= v[2] && v[2] <= v[3]) ++ordOk;
                int perm[4] = {0, 1, 2, 3}, pi = 0;
                do {
                    if (v[perm[0]] <= v[perm[1]] && v[perm[1]] <= v[perm[2]] && v[perm[2]] <= v[perm[3]]) ++ordAny[pi];
                    ++pi;
                } while (std::next_permutation(perm, perm + 4));
                const double c3v = fAt(d3, k, 0, 4);
                minDen = std::min(minDen, 2 * c3v + 1);
                ++c3n;
                if (c3v > 0) ++c3pos;
            }
        }
        printf("ch0 position keys: %lld, all finite %lld (spec 14,703 keys, all finite)\n", posN, finite);
        px.print("pos x (spec -453..1247)"); py.print("pos y (spec -324..588)"); pz.print("pos z (spec -869..735)");
        fov.print("FOV radians");
        printf("  FOV keys in (0, pi): %lld / %lld (spec 2,345/2,345); in degrees (x57.2957763671875): %.3f .. %.3f, median %.3f\n",
               fovIn, fovN, fov.mn * 57.2957763671875, fov.mx * 57.2957763671875, pct(fov.all, 0.5) * 57.2957763671875);
        static const char* dn[5] = {"ch3 CoC scale/gate", "ch4 Focal.x", "ch5 Focal.y", "ch6 Focal.z", "ch7 Focal.w"};
        for (int c = 0; c < 5; ++c) dof[c].print(dn[c]);
        printf("  spec Sec5.2 ranges: ch3 -0.150..1.000 med 0.150; ch4 -22.6..50.0 med 0.149; ch5 -6.8..140.9 med 1.785; ch6 -27.3..2500.5 med 7.281; ch7 0.848..3986.8 med 30.000\n");
        printf("  DOF ch4<=ch5<=ch6<=ch7 (per key): %lld / %lld (%.2f%%) (spec 3,929/3,958 = 99.27%%)\n", ordOk, ordN,
               100.0 * static_cast<double>(ordOk) / static_cast<double>(std::max<long long>(1, ordN)));
        long long bestOther = 0;
        for (int i = 1; i < 24; ++i) bestOther = std::max(bestOther, ordAny[i]);
        printf("     control: best of the 23 other orderings: %lld / %lld (%.2f%%) (spec 0.8%%)\n", bestOther, ordN,
               100.0 * static_cast<double>(bestOther) / static_cast<double>(std::max<long long>(1, ordN)));
        printf("  ch3 gate: c3 > 0 (three-layer variant) on %lld / %lld DOF keys (spec 98.3%%); min(2*c3+1) = %.4f (spec 0.700; never singular)\n",
               c3pos, c3n, minDen);
        // control: DOF channels read as FOV
        for (int c = 0; c < 5; ++c) {
            long long in = 0;
            for (double v : dof[c].all) if (v > 0 && v < 3.14159265358979323846) ++in;
            printf("     control: ch%d read as FOV in (0,pi): %lld / %zu (%.1f%%)\n", 3 + c, in, dof[c].all.size(),
                   100.0 * static_cast<double>(in) / static_cast<double>(std::max<size_t>(1, dof[c].all.size())));
        }
        // position continuity vs random
        std::vector<double> adj, rp;
        std::vector<std::array<double, 3>> flat;
        for (const auto& f : files) for (const auto& sh : f.shots) {
            const Track& p = sh.ch[0];
            for (size_t k = 0; k < p.n; ++k) {
                std::array<double, 3> v = {fAt(p, k, 0, 12), fAt(p, k, 1, 12), fAt(p, k, 2, 12)};
                if (k > 0) adj.push_back(std::sqrt((v[0] - flat.back()[0]) * (v[0] - flat.back()[0]) + (v[1] - flat.back()[1]) * (v[1] - flat.back()[1]) + (v[2] - flat.back()[2]) * (v[2] - flat.back()[2])));
                flat.push_back(v);
            }
        }
        for (size_t i = 0; i < adj.size(); ++i) {
            const auto& a = flat[rnd() % flat.size()];
            const auto& b = flat[rnd() % flat.size()];
            rp.push_back(std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2])));
        }
        printf("  position step between consecutive keys: median %.4f p99 %.3f; control randomly paired: median %.2f (spec 0.0726/6.94 vs 48.2)\n",
               pct(adj, 0.5), pct(adj, 0.99), pct(rp, 0.5));
    }

    // ---------------------------------------- sampler behaviour (indep) ----
    printf("\n================ SAMPLER MODEL (independent implementation, Sec10.3) ================\n");
    {
        long long atKey = 0, atKeyN = 0, holdAfter = 0, holdBefore = 0, holdN = 0;
        double worstKey = 0;
        long long midScalarOk = 0, midScalarN = 0, midNormN = 0, midNormOk = 0, lerpNormOk = 0, farN = 0, farSlerpOk = 0, farLerpOk = 0;
        double minMidNorm = 10;
        for (const auto& f : files) for (const auto& sh : f.shots) {
            for (size_t ch = 0; ch < 8; ++ch) {
                const Track& tr = sh.ch[ch];
                if (tr.n == 0) continue;
                for (size_t k = 0; k < tr.n; ++k) {
                    const SampledIndep s = sampleIndep(sh, tr.t[k]);
                    // the value at a key time must equal that key (for strictly ascending tracks)
                    double diff = 0;
                    if (ch == 0) for (size_t c = 0; c < 3; ++c) diff = std::max(diff, std::fabs(s.pos[c] - fAt(tr, k, c, 12)));
                    else if (ch == 1) { double q[4]; keyQuat(tr, k, q); for (int c = 0; c < 4; ++c) diff = std::max(diff, std::fabs(s.q[c] - q[c])); }
                    else if (ch == 2) diff = std::fabs(s.fov - fAt(tr, k, 0, 4));
                    else diff = std::fabs(s.dofv[ch - 3] - fAt(tr, k, 0, 4));
                    ++atKeyN;
                    if (diff == 0) ++atKey;
                    worstKey = std::max(worstKey, diff);
                }
                // hold before first / after last
                const double tBefore = static_cast<double>(tr.t[0]) - 5.0, tAfter = static_cast<double>(tr.t.back()) + 100.0;
                const SampledIndep sb = sampleIndep(sh, tBefore), sa = sampleIndep(sh, tAfter);
                const SampledIndep s0 = sampleIndep(sh, tr.t[0]), s1 = sampleIndep(sh, tr.t.back());
                auto same = [&](const SampledIndep& a, const SampledIndep& b) {
                    if (ch == 0) return a.pos[0] == b.pos[0] && a.pos[1] == b.pos[1] && a.pos[2] == b.pos[2];
                    if (ch == 1) return a.q[0] == b.q[0] && a.q[1] == b.q[1] && a.q[2] == b.q[2] && a.q[3] == b.q[3];
                    if (ch == 2) return a.fov == b.fov;
                    return a.dofv[ch - 3] == b.dofv[ch - 3];
                };
                ++holdN;
                if (same(sb, s0)) ++holdBefore;
                if (same(sa, s1)) ++holdAfter;
                // scalar midpoints
                if (ch == 2 || ch >= 3) {
                    for (size_t k = 1; k < tr.n; ++k) {
                        const double a = tr.t[k - 1], b = tr.t[k];
                        if (!(b > a)) continue;
                        const double tm = 0.5 * (a + b);
                        const double got = scalarIndep(tr, tm);
                        const double want = 0.5 * (fAt(tr, k - 1, 0, 4) + fAt(tr, k, 0, 4));
                        ++midScalarN;
                        if (std::fabs(got - want) <= 1e-9 * (1 + std::fabs(want))) ++midScalarOk;
                    }
                }
                if (ch == 1) {
                    for (size_t k = 1; k < tr.n; ++k) {
                        const double a = tr.t[k - 1], b = tr.t[k];
                        if (!(b > a)) continue;
                        double q0[4], q1[4], o[4];
                        keyQuat(tr, k - 1, q0);
                        keyQuat(tr, k, q1);
                        slerpIndep(q0, q1, 0.5, o);
                        const double nrm = std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2] + o[3] * o[3]);
                        ++midNormN;
                        minMidNorm = std::min(minMidNorm, nrm);
                        if (std::fabs(nrm - 1) <= TOL) ++midNormOk;
                        // control: plain component lerp (no slerp, no renormalise); sign-aligned
                        double sgn = (q0[0] * q1[0] + q0[1] * q1[1] + q0[2] * q1[2] + q0[3] * q1[3]) < 0 ? -1 : 1;
                        double l[4];
                        for (int c = 0; c < 4; ++c) l[c] = 0.5 * q0[c] + 0.5 * sgn * q1[c];
                        const double ln = std::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2] + l[3] * l[3]);
                        if (std::fabs(ln - 1) <= TOL) ++lerpNormOk;
                        const double c01 = std::fabs(q0[0] * q1[0] + q0[1] * q1[1] + q0[2] * q1[2] + q0[3] * q1[3]);
                        if (2.0 * std::acos(std::min(1.0, c01)) * 180.0 / 3.14159265358979323846 > 10.0) {
                            ++farN;
                            if (std::fabs(nrm - 1) <= TOL) ++farSlerpOk;
                            if (std::fabs(ln - 1) <= TOL) ++farLerpOk;
                        }
                    }
                }
            }
        }
        printf("value at a key time == that key, bit-exact: %lld / %lld (worst abs diff %.3g)   [model self-test]\n", atKey, atKeyN, worstKey);
        printf("hold before first key: %lld / %lld ; hold after last key: %lld / %lld   [model self-test]\n", holdBefore, holdN, holdAfter, holdN);
        printf("scalar midpoint == mean of the two keys (1e-9 rel): %lld / %lld\n", midScalarOk, midScalarN);
        printf("ch1 slerp midpoints unit-norm within TOL: %lld / %lld (min norm %.6f);  control plain-lerp midpoints: %lld / %lld\n",
               midNormOk, midNormN, minMidNorm, lerpNormOk, midNormN);
        printf("   restricted to steps > 10 deg apart: slerp %lld / %lld unit within TOL, lerp %lld / %lld (the steps that separate slerp from plain lerp)\n",
               farSlerpOk, farN, farLerpOk, farN);
        // hard cut: jump in position across shot boundaries
        std::vector<double> jump;
        for (const auto& f : files) for (size_t s = 1; s < f.shots.size(); ++s) {
            const Track& a = f.shots[s - 1].ch[0];
            const Track& b = f.shots[s].ch[0];
            if (!a.n || !b.n) continue;
            double d2 = 0;
            for (size_t c = 0; c < 3; ++c) { const double d = fAt(a, a.n - 1, c, 12) - fAt(b, 0, c, 12); d2 += d * d; }
            jump.push_back(std::sqrt(d2));
        }
        long long big = 0;
        for (double j : jump) if (j > 1.0) ++big;
        printf("shot boundaries: distance between shot k's last ch0 key and shot k+1's first: median %.3f, p90 %.3f; > 1 unit on %lld / %zu boundaries\n",
               pct(jump, 0.5), pct(jump, 0.9), big, jump.size());
        printf("   (consistent with hard cuts - a blend would need the values to meet; but this cannot prove there is no downstream smoothing, Sec10.6.4 OPEN)\n");
    }

    // ============================== PART 2: library vs independent decode ===
    printf("\n================ PART 2: sr3cutscene library vs independent decode ================\n");
    {
        long long filesOk = 0, filesBad = 0, shotsCmp = 0, rawEq = 0, rawTot = 0, decEq = 0, decTot = 0, shapeMis = 0;
        long long samples = 0, idxMismatch = 0;
        double worst = 0;
        bool sampleThrew = false;
        for (const auto& f : files) {
            const std::vector<uint8_t>& bytes = g_distinct.at(f.hash);
            sr3cutscene::CameraScript cs;
            try {
                cs = sr3cutscene::CameraScript::parse(sr3cutscene::ByteView(bytes.data(), bytes.size()));
            } catch (const std::exception& ex) {
                ++filesBad;
                printf("  library parse FAILED for %s: %s\n", f.name.c_str(), ex.what());
                continue;
            }
            ++filesOk;
            if (cs.shots().size() != f.shots.size()) { ++shapeMis; continue; }
            for (size_t s = 0; s < f.shots.size(); ++s) {
                ++shotsCmp;
                const Shot& is = f.shots[s];
                const sr3cutscene::Shot& ls = cs.shots()[s];
                for (size_t ch = 0; ch < 8; ++ch) {
                    if (ls.channels[ch].keyCount != is.ch[ch].n || ls.channels[ch].times != is.ch[ch].t ||
                        ls.channels[ch].valueBytes != is.ch[ch].v) ++shapeMis;
                }
                // raw + decoded quaternions
                const auto raw = sr3cutscene::decodeQuaternionsRaw(ls.channels[1]);
                const auto dec = sr3cutscene::decodeQuaternions(ls.channels[1]);
                for (size_t k = 0; k < is.ch[1].n; ++k) {
                    ++rawTot;
                    ++decTot;
                    bool re = k < raw.size(), de = k < dec.size();
                    for (int c = 0; c < 4 && re; ++c) re = raw[k][static_cast<size_t>(c)] == rds16(is.ch[1].v.data() + k * 8 + static_cast<size_t>(c) * 2);
                    if (de) {
                        double q[4];
                        keyQuat(is.ch[1], k, q);
                        de = dec[k].x == q[0] && dec[k].y == q[1] && dec[k].z == q[2] && dec[k].w == q[3];
                    }
                    if (re) ++rawEq;
                    if (de) ++decEq;
                }
                // sample times: every key time of every channel, the shot ends, and 48 spread points beyond the ends
                std::vector<double> ts;
                for (int ch = 0; ch < 8; ++ch) for (float tv : is.ch[ch].t) { ts.push_back(tv); ts.push_back(static_cast<double>(tv) + 0.013); }
                for (int i = -6; i <= 41; ++i) ts.push_back(is.start + (static_cast<double>(is.end) - is.start) * i / 40.0);
                for (double t : ts) {
                    try {
                        const sr3cutscene::CameraSample a = cs.sampleShot(s, t);
                        const SampledIndep b = sampleIndep(is, t);
                        ++samples;
                        double d = 0;
                        for (size_t c = 0; c < 3; ++c) d = std::max(d, std::fabs(a.position[c] - b.pos[c]));
                        d = std::max(d, std::fabs(a.orientation.x - b.q[0]));
                        d = std::max(d, std::fabs(a.orientation.y - b.q[1]));
                        d = std::max(d, std::fabs(a.orientation.z - b.q[2]));
                        d = std::max(d, std::fabs(a.orientation.w - b.q[3]));
                        d = std::max(d, std::fabs(a.fovRadians - b.fov));
                        d = std::max(d, std::fabs(a.fovDegrees - b.fov * 57.2957763671875));
                        if (a.dofEnabled != b.dof) d = 1e9;
                        if (b.dof) {
                            d = std::max(d, std::fabs(a.dofCocScale - b.dofv[0]));
                            for (size_t c = 0; c < 4; ++c) d = std::max(d, std::fabs(a.dofFocalParams[c] - b.dofv[1 + c]));
                        }
                        worst = std::max(worst, d);
                    } catch (const std::exception&) {
                        sampleThrew = true;
                    }
                }
            }
            // active-shot selection: start-of-shot times select that shot on a tiling file
            for (size_t s = 0; s < f.shots.size(); ++s) {
                const double mid = 0.5 * (static_cast<double>(f.shots[s].start) + f.shots[s].end);
                if (cs.shotIndexAt(mid) != s) ++idxMismatch;
            }
        }
        printf("library parse OK: %lld / %zu files; shot/channel/key-count/times/valueBytes mismatches vs independent parse: %lld\n", filesOk, files.size(), shapeMis);
        printf("decodeQuaternionsRaw == independent s16 quads: %lld / %lld ; decodeQuaternions == independent decode (bit-equal doubles): %lld / %lld\n", rawEq, rawTot, decEq, decTot);
        printf("sampleShot vs independent sampler at %lld times (all key times, +13 ms, and 48 points spanning [start-15%%, end+2.5%%]): worst abs diff %.3g (sample threw: %s)\n",
               samples, worst, sampleThrew ? "YES" : "no");
        printf("shotIndexAt(mid-shot) != the shot: %lld (shots tile, so the start/end scan is unambiguous)\n", idxMismatch);
        (void)filesBad;
    }

    printf("\nEND\n");
    return 0;
}
