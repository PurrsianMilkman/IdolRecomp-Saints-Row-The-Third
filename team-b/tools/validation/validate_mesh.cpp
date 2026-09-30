// Real-data validation for sr3mesh: reproduce spec-vertex-format.md's own
// population statistics before trusting the reader to feed a renderer.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_block.h"
#include "sr3mesh/mesh_block.h"
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

bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView raw = c.rawEntryBytes(i);
        out.assign(raw.data(), raw.data() + raw.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    bool usable = r.status == vpp::DecodeStatus::Ok ||
                  r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
                  r.status == vpp::DecodeStatus::ContentValidated ||
                  r.status == vpp::DecodeStatus::RecoveredSharedStream ||
                  r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!usable) return false;
    out = std::move(r.data);
    return true;
}

int g_pairs = 0, g_ok = 0, g_fail = 0;
int g_gLenMatchesFileSize = 0;
int g_divisibleByThree = 0;
long long g_channels = 0, g_strideLawOk = 0;
long long g_vertices = 0, g_finitePositions = 0;
long long g_indices = 0, g_indicesInRange = 0;
std::map<int, long long> g_layoutCodes;
std::map<int, long long> g_texcoordCounts;
std::set<int> g_indexElementSizes;
long long g_weightVerts = 0, g_weightSumOk = 0, g_laneAgree = 0, g_laneTotal = 0;
long long g_normalUnit = 0, g_normalTotal = 0;
// Texcoord encoding comparison (spec open item 5).
long long g_uvHalfExpZero = 0, g_uvOver32767 = 0, g_uvTotal = 0, g_uvCanonical = 0;
float g_uvMin = 1e30f, g_uvMax = -1e30f;
int32_t g_rawMin = 1000000, g_rawMax = -1000000;
int g_dumped = 0;

void check(const std::string& name, const std::vector<uint8_t>& cb, const std::vector<uint8_t>& gb) {
    try {
        sr3geometry::MaterialBlock mat =
            sr3geometry::MaterialBlock::parse(vpp::ByteView(cb.data(), cb.size()));
        sr3geometry::GeometryBlock geo =
            sr3geometry::GeometryBlock::parse(vpp::ByteView(cb.data(), cb.size()), mat);
        if (!geo.hasMeshSubBlock()) return;
        ++g_pairs;

        sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(
            vpp::ByteView(cb.data(), cb.size()), geo.meshSubBlockOffset(),
            vpp::ByteView(gb.data(), gb.size()));
        ++g_ok;

        if (mesh.gLength() == gb.size()) ++g_gLenMatchesFileSize;
        if (mesh.indexCountDivisibleByThree()) ++g_divisibleByThree;
        g_indexElementSizes.insert(mesh.indexElementSize());

        // Index range test (spec §8): every index < vertex count.
        uint32_t vertexCount = mesh.channels().empty() ? 0 : mesh.channels()[0].elementCount;
        for (uint16_t idx : mesh.indices()) {
            ++g_indices;
            if (vertexCount != 0 && idx < vertexCount) ++g_indicesInRange;
        }

        for (size_t ci = 0; ci < mesh.channels().size(); ++ci) {
            const auto& ch = mesh.channels()[ci];
            ++g_channels;
            if (ch.strideMatchesLaw) ++g_strideLawOk;
            ++g_layoutCodes[ch.layoutCode];
            ++g_texcoordCounts[ch.texcoordCount];

            sr3mesh::LayoutInfo info = sr3mesh::layoutInfoFor(ch.layoutCode);
            if (!info.known) continue;

            std::vector<sr3mesh::Vertex> verts = mesh.decodeChannel(ci);
            for (const auto& v : verts) {
                ++g_vertices;
                if (std::isfinite(v.position[0]) && std::isfinite(v.position[1]) &&
                    std::isfinite(v.position[2])) {
                    ++g_finitePositions;
                }
                if (info.hasNormal) {
                    ++g_normalTotal;
                    float len = std::sqrt(v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] +
                                          v.normal[2] * v.normal[2]);
                    if (len > 0.94f && len < 1.06f) ++g_normalUnit;
                }
                if (info.hasSkinning) {
                    ++g_weightVerts;
                    int sum = 0;
                    for (int i = 0; i < 4; ++i) sum += static_cast<int>(v.blendWeights[i] * 255.0f + 0.5f);
                    if (sum >= 250 && sum <= 260) ++g_weightSumOk;
                    for (int i = 0; i < 4; ++i) {
                        ++g_laneTotal;
                        bool weightZero = v.blendWeights[i] < 0.5f / 255.0f;
                        bool indexSentinel = v.blendIndices[i] == 255;
                        if (weightZero == indexSentinel) ++g_laneAgree;
                    }
                }
                for (size_t t = 0; t < v.texcoords.size(); ++t) {
                    // Verify the CORRECTED encoding independently rather
                    // than taking the correction on faith. The decisive
                    // statistic is the half-float exponent field: if 94.6%
                    // of raw values have exponent 0, half-float would
                    // decode nearly everything as a ~0.0001 denormal.
                    for (int comp = 0; comp < 2; ++comp) {
                        ++g_uvTotal;
                        uint16_t raw = static_cast<uint16_t>(v.texcoordsRaw[t][comp]);
                        if (((raw >> 10) & 0x1F) == 0) ++g_uvHalfExpZero;
                        if (raw > 32767) ++g_uvOver32767;
                        int32_t signedRaw = v.texcoordsRaw[t][comp];
                        if (signedRaw > g_rawMax) g_rawMax = signedRaw;
                        if (signedRaw < g_rawMin) g_rawMin = signedRaw;
                    }
                    float u = v.texcoords[t][0], w = v.texcoords[t][1];
                    if (u < g_uvMin) g_uvMin = u;
                    if (u > g_uvMax) g_uvMax = u;
                    if (w < g_uvMin) g_uvMin = w;
                    if (w > g_uvMax) g_uvMax = w;
                    if (u >= -0.05f && u <= 1.05f && w >= -0.05f && w <= 1.05f) ++g_uvCanonical;
                }
            }

            if (g_dumped < 4 && ci == 0 && !verts.empty()) {
                ++g_dumped;
                float mnx = 1e30f, mny = 1e30f, mnz = 1e30f, mxx = -1e30f, mxy = -1e30f, mxz = -1e30f;
                for (const auto& v : verts) {
                    mnx = v.position[0] < mnx ? v.position[0] : mnx;
                    mny = v.position[1] < mny ? v.position[1] : mny;
                    mnz = v.position[2] < mnz ? v.position[2] : mnz;
                    mxx = v.position[0] > mxx ? v.position[0] : mxx;
                    mxy = v.position[1] > mxy ? v.position[1] : mxy;
                    mxz = v.position[2] > mxz ? v.position[2] : mxz;
                }
                printf("  %-34s layout=%-3u tc=%u stride=%zu verts=%zu idx=%u(%s)  bbox %.3f x %.3f x %.3f (minY %.3f)\n",
                       name.c_str(), ch.layoutCode, ch.texcoordCount, ch.stride(), verts.size(),
                       mesh.indexCount(), mesh.indexCountDivisibleByThree() ? "/3 ok" : "NOT /3",
                       mxx - mnx, mxy - mny, mxz - mnz, mny);
            }
        }
    } catch (const sr3mesh::FormatError& ex) {
        ++g_fail;
        if (g_fail <= 10) printf("  MESH FAIL %s: %s\n", name.c_str(), ex.what());
    } catch (const std::exception&) {
    }
}

void walk(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& name = c.entries()[i].name;
        if (endsWith(name, ".ccmesh_pc") || endsWith(name, ".csmesh_pc")) {
            std::string gname = name;
            size_t dot = gname.find_last_of('.');
            if (dot != std::string::npos) gname[dot + 1] = 'g';
            std::vector<uint8_t> cb, gb;
            if (!entryBytes(c, i, cb)) continue;
            for (size_t j = 0; j < c.entries().size(); ++j) {
                if (c.entries()[j].name == gname) { entryBytes(c, j, gb); break; }
            }
            if (!gb.empty()) check(name, cb, gb);
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walk(c.openNested(i)); } catch (const std::exception&) {}
        }
    }
}

int main(int argc, char** argv) {
    printf("sample meshes:\n");
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> bytes = readFile(argv[i]);
        if (bytes.empty()) continue;
        try {
            vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
            walk(c);
            printf("scanned %s\n", argv[i]);
            fflush(stdout);
        } catch (const std::exception&) {}
    }

    printf("\n=== sr3mesh validation (spec-vertex-format.md) ===\n");
    printf("paired meshes with a Mesh block : %d\n", g_pairs);
    printf("parsed OK (full walk + bookends): %d\n", g_ok);
    printf("failed                          : %d\n", g_fail);
    printf("declared g-length == g-file size: %d / %d   (spec Sec4.1: 1937/1937)\n",
           g_gLenMatchesFileSize, g_ok);
    printf("index element size values seen  : ");
    for (int s : g_indexElementSizes) printf("%d ", s);
    printf("  (spec Sec8: always 2)\n");
    printf("index count divisible by 3      : %d / %d (%.1f%%)   (spec Sec8: ~62%%)\n",
           g_divisibleByThree, g_ok, g_ok ? 100.0 * g_divisibleByThree / g_ok : 0.0);
    printf("indices < vertex count          : %lld / %lld   (spec Sec8: 1.000000)\n",
           g_indicesInRange, g_indices);

    printf("\n-- channels --\n");
    printf("channels read                   : %lld\n", g_channels);
    printf("stride matches the spec's law   : %lld / %lld   (spec Sec5: 26601/26601)\n",
           g_strideLawOk, g_channels);
    printf("layout codes seen               : ");
    for (const auto& kv : g_layoutCodes) printf("%d(x%lld) ", kv.first, kv.second);
    printf("\ntexcoord counts seen            : ");
    for (const auto& kv : g_texcoordCounts) printf("%d(x%lld) ", kv.first, kv.second);

    printf("\n\n-- vertex fields --\n");
    printf("positions finite                : %lld / %lld   (spec Sec6.1: all)\n",
           g_finitePositions, g_vertices);
    printf("normals unit length (6%% tol)    : %lld / %lld   (spec Sec6.2: 1.000)\n",
           g_normalUnit, g_normalTotal);
    printf("blend weight sums in [250,260]  : %lld / %lld   (spec Sec6.3: all)\n",
           g_weightSumOk, g_weightVerts);
    printf("weight-zero <=> index-255 lanes : %lld / %lld   (spec Sec6.3: 1.000000)\n",
           g_laneAgree, g_laneTotal);

    printf("\n-- texcoord encoding (spec Sec6.5, CORRECTED to int16/1024) --\n");
    printf("coordinate values               : %lld\n", g_uvTotal);
    printf("half-float exponent field == 0  : %lld / %lld (%.2f%%)   (spec: 94.6%% -> refutes half-float)\n",
           g_uvHalfExpZero, g_uvTotal, g_uvTotal ? 100.0 * g_uvHalfExpZero / g_uvTotal : 0.0);
    printf("raw values > 32767              : %lld / %lld (%.2f%%)   (spec: 2.79%% -> must read SIGNED)\n",
           g_uvOver32767, g_uvTotal, g_uvTotal ? 100.0 * g_uvOver32767 / g_uvTotal : 0.0);
    printf("raw signed range                : %d .. %d   (spec: peaks near 1021 = 1.0 at scale 1024)\n",
           g_rawMin, g_rawMax);
    printf("decoded range (int16/1024)      : %.4f .. %.4f   (spec median: -0.0098 .. 1.0059)\n",
           g_uvMin, g_uvMax);
    printf("UV pairs within [-0.05, 1.05]   : %lld / %lld (%.2f%%)\n",
           g_uvCanonical, g_uvTotal / 2, g_uvTotal ? 100.0 * g_uvCanonical / (g_uvTotal / 2) : 0.0);
    return g_fail == 0 ? 0 : 1;
}
