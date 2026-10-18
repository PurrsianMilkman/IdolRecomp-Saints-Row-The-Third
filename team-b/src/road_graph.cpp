#include "sr3zone/road_graph.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace sr3zone {

namespace {

constexpr uint32_t kTrafficTag = 0x80002236u;
constexpr uint32_t kSidewalksTag = 0x80002238u;
constexpr size_t kTagHeader = 12;
constexpr size_t kTrafficHeader = 0x78;
constexpr size_t kSegStride = 64, kNodeStride = 12;
constexpr size_t kSwNodeStride = 24, kSwEdgeStride = 8, kSwCrossStride = 36;
constexpr size_t kSwHeader = 0x28;

[[noreturn]] void Fail(const std::string& what) { throw FormatError("road graph: " + what); }

uint32_t U32(vpp::ByteView v, size_t o) { return v.readU32LE(o); }
int32_t S32(vpp::ByteView v, size_t o) { return static_cast<int32_t>(v.readU32LE(o)); }
uint16_t U16(vpp::ByteView v, size_t o) { return v.readU16LE(o); }
int16_t S16(vpp::ByteView v, size_t o) { return static_cast<int16_t>(v.readU16LE(o)); }
float F32(vpp::ByteView v, size_t o) {
    uint32_t u = v.readU32LE(o);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
RoadVec3 V3(vpp::ByteView v, size_t o) { return {F32(v, o), F32(v, o + 4), F32(v, o + 8)}; }

// {count, payload-relative offset}; throws unless the array fits in the payload.
struct Pair {
    size_t count = 0, off = 0;
};
Pair ReadPair(vpp::ByteView payload, size_t hdrOff, size_t stride, const char* what) {
    const int32_t c = S32(payload, hdrOff), o = S32(payload, hdrOff + 4);
    Pair p;
    if (c <= 0) return p;
    if (o < 0) Fail(std::string(what) + ": negative offset");
    p.count = static_cast<size_t>(c);
    p.off = static_cast<size_t>(o);
    if (p.off + p.count * stride > payload.size()) Fail(std::string(what) + ": array outside the payload");
    return p;
}

// Finds a 4-aligned `tag` whose 12-byte header ({tag, length, 0}) is followed
// by a payload of at least `minPayload` bytes that the caller then validates.
// `length` is checked against the file size only (its exact meaning is not
// needed to read the payload; Sec13.2a(a) gives the payload sizes).
bool FindSection(vpp::ByteView czn, uint32_t tag, size_t minPayload, size_t& payloadOff) {
    for (size_t o = 0; o + kTagHeader + minPayload <= czn.size(); o += 4) {
        if (U32(czn, o) != tag) continue;
        if (U32(czn, o + 8) != 0) continue;  // third dword is 0 (Sec13.2a(a))
        payloadOff = o + kTagHeader;
        return true;
    }
    return false;
}

}  // namespace

RoadTile RoadTileOf(double x, double z) {
    // Sec10.6(g), std::floor(v + 0.5) = round half up.
    const int row = static_cast<int>(std::floor((3640.0 - z) / 280.0 + 0.5));
    const int odd = ((row % 2) + 2) % 2;
    const int col = static_cast<int>(std::floor((x + 4000.0 + 160.0 * odd) / 320.0 + 0.5));
    return {col, row};
}

RoadGraph RoadGraph::Parse(vpp::ByteView czn) {
    RoadGraph g;

    // ---------------- Traffic (0x2236) ----------------
    size_t tOff = 0;
    if (!FindSection(czn, kTrafficTag, kTrafficHeader, tOff)) Fail("Traffic (0x2236) section not found");
    vpp::ByteView tp = czn.subview(tOff, czn.size() - tOff);

    const Pair segP = ReadPair(tp, 0x00, kSegStride, "segments");
    const Pair nodeP = ReadPair(tp, 0x08, kNodeStride, "nodes");
    const Pair succP = ReadPair(tp, 0x10, 2, "successor pool");
    const Pair predP = ReadPair(tp, 0x18, 2, "predecessor pool");
    const Pair confP = ReadPair(tp, 0x20, 2, "conflict pool");
    const Pair shapeP = ReadPair(tp, 0x28, 2, "shape pool");
    if (segP.count == 0 || nodeP.count == 0) Fail("Traffic: empty segment/node array");
    if (shapeP.count % 3 != 0) Fail("shape pool count not a multiple of 3");

    g.nodes.reserve(nodeP.count);
    for (size_t i = 0; i < nodeP.count; ++i) g.nodes.push_back(V3(tp, nodeP.off + i * kNodeStride));

    g.segments.reserve(segP.count);
    for (size_t i = 0; i < segP.count; ++i) {
        const size_t b = segP.off + i * kSegStride;
        RoadSegment s;
        s.nodeA = U16(tp, b + 0x00);
        s.nodeB = U16(tp, b + 0x02);
        s.ctrl1 = V3(tp, b + 0x04);
        s.ctrl2 = V3(tp, b + 0x10);
        s.flags = U32(tp, b + 0x1c);
        s.chord256 = U16(tp, b + 0x20);
        s.halfArc256 = U16(tp, b + 0x22);
        for (int k = 0; k < 3; ++k) {
            s.aabbMin[k] = S16(tp, b + 0x24 + 2 * k);
            s.aabbMax[k] = S16(tp, b + 0x2a + 2 * k);
        }
        s.signal = tp.at(b + 0x30);
        s.succRef = U16(tp, b + 0x34);
        s.predRef = U16(tp, b + 0x36);
        s.conflictRef = U16(tp, b + 0x38);
        s.right = U16(tp, b + 0x3a);
        s.left = U16(tp, b + 0x3c);
        s.shapeFirst = U16(tp, b + 0x3e);
        if (s.nodeA >= nodeP.count || s.nodeB >= nodeP.count) Fail("segment node index out of range");
        g.segments.push_back(s);
    }
    auto readPool = [&](const Pair& p, std::vector<uint16_t>& out) {
        out.resize(p.count);
        for (size_t i = 0; i < p.count; ++i) out[i] = U16(tp, p.off + i * 2);
    };
    readPool(succP, g.succPool);
    readPool(predP, g.predPool);
    readPool(confP, g.conflictPool);
    g.shapePool.resize(shapeP.count);
    for (size_t i = 0; i < shapeP.count; ++i) g.shapePool[i] = S16(tp, shapeP.off + i * 2);

    // Signals (Sec13.2a(i)): lights +0x48 (16 B), junctions +0x50 (20 B), phases +0x58 (6 B), light list +0x60, lane list +0x68.
    {
        const Pair lightP = ReadPair(tp, 0x48, 16, "signal lights");
        const Pair juncP = ReadPair(tp, 0x50, 20, "signal junctions");
        const Pair phaseP = ReadPair(tp, 0x58, 6, "signal phases");
        const Pair plP = ReadPair(tp, 0x60, 2, "phase light list");
        const Pair pnP = ReadPair(tp, 0x68, 2, "phase lane list");
        g.lights.reserve(lightP.count);
        for (size_t i = 0; i < lightP.count; ++i) {
            const size_t b = lightP.off + i * 16;
            SignalLight l;
            l.id = static_cast<uint64_t>(U32(tp, b)) | (static_cast<uint64_t>(U32(tp, b + 4)) << 32);
            l.objectIndex = S32(tp, b + 0x08);
            l.zoneId = U16(tp, b + 0x0c);
            l.vehicleState = tp.at(b + 0x0e);
            l.pedState = tp.at(b + 0x0f);
            g.lights.push_back(l);
        }
        g.junctions.reserve(juncP.count);
        for (size_t i = 0; i < juncP.count; ++i) {
            const size_t b = juncP.off + i * 20;
            SignalJunction j;
            j.pos = V3(tp, b);
            j.firstPhase = U16(tp, b + 0x0c);
            j.currentPhase = tp.at(b + 0x0e);
            j.byte0f = tp.at(b + 0x0f);
            j.timer = S32(tp, b + 0x10);
            if (j.firstPhase > phaseP.count || (i > 0 && j.firstPhase < g.junctions.back().firstPhase))
                Fail("signal junction first phase out of order/range");
            g.junctions.push_back(j);
        }
        g.phases.reserve(phaseP.count);
        for (size_t i = 0; i < phaseP.count; ++i) {
            const size_t b = phaseP.off + i * 6;
            SignalPhase p;
            p.firstLight = U16(tp, b);
            p.firstLane = U16(tp, b + 2);
            p.duration = U16(tp, b + 4);
            if (p.firstLight > plP.count || p.firstLane > pnP.count ||
                (i > 0 && (p.firstLight < g.phases.back().firstLight || p.firstLane < g.phases.back().firstLane)))
                Fail("signal phase list start out of order/range");
            g.phases.push_back(p);
        }
        readPool(plP, g.phaseLights);
        readPool(pnP, g.phaseLanes);
        for (uint16_t l : g.phaseLights)
            if (l >= g.lights.size()) Fail("phase light index out of range");
        for (uint16_t s : g.phaseLanes)
            if (s >= g.segments.size()) Fail("phase lane index out of range");
    }

    // Link sanity (Sec13.2a(e)): every reference resolves in range.
    for (size_t i = 0; i < g.segments.size(); ++i) {
        for (uint16_t j : g.Successors(i))
            if (j >= g.segments.size()) Fail("successor index out of range");
        for (uint16_t j : g.Predecessors(i))
            if (j >= g.segments.size()) Fail("predecessor index out of range");
        for (uint16_t j : g.Conflicts(i))
            if (j >= g.segments.size()) Fail("conflict index out of range");
    }

    // ---------------- Sidewalks (0x2238) ----------------
    size_t sOff = 0;
    if (!FindSection(czn, kSidewalksTag, kSwHeader, sOff)) Fail("Sidewalks (0x2238) section not found");
    vpp::ByteView sp = czn.subview(sOff, czn.size() - sOff);
    const Pair swNodeP = ReadPair(sp, 0x00, kSwNodeStride, "sidewalk nodes");
    const Pair swAdjP = ReadPair(sp, 0x08, 2, "sidewalk adjacency");
    const Pair swEdgeP = ReadPair(sp, 0x10, kSwEdgeStride, "sidewalk edges");
    const Pair swCrossP = ReadPair(sp, 0x18, kSwCrossStride, "sidewalk crossings");
    if (swNodeP.count == 0 || swEdgeP.count == 0) Fail("Sidewalks: empty node/edge array");
    g.sidewalkNodes.reserve(swNodeP.count);
    for (size_t i = 0; i < swNodeP.count; ++i) {
        const size_t b = swNodeP.off + i * kSwNodeStride;
        SidewalkNode n;
        n.pos = V3(sp, b);
        n.byte0c = sp.at(b + 0x0c);
        n.byte0d = sp.at(b + 0x0d);
        n.adjFirst = U16(sp, b + 0x0e);
        g.sidewalkNodes.push_back(n);
    }
    // Crossings (Sec13.2a(l)): +0x00 s32 byte offset into the Traffic light array (16 B records); -1 = no light.
    g.crossingLight.reserve(swCrossP.count);
    for (size_t i = 0; i < swCrossP.count; ++i) {
        const int32_t off = S32(sp, swCrossP.off + i * kSwCrossStride);
        if (off < 0) {
            g.crossingLight.push_back(-1);
            continue;
        }
        if (off % 16 != 0 || static_cast<size_t>(off) / 16 >= g.lights.size()) Fail("crossing light offset out of range");
        g.crossingLight.push_back(off / 16);
    }
    g.sidewalkAdj.resize(swAdjP.count);
    for (size_t i = 0; i < swAdjP.count; ++i) g.sidewalkAdj[i] = U16(sp, swAdjP.off + i * 2);
    g.sidewalkEdges.reserve(swEdgeP.count);
    for (size_t i = 0; i < swEdgeP.count; ++i) {
        const size_t b = swEdgeP.off + i * kSwEdgeStride;
        SidewalkEdge e;
        e.nodeA = U16(sp, b);
        e.nodeB = U16(sp, b + 2);
        e.width256 = U16(sp, b + 4);
        e.crossing = U16(sp, b + 6);
        if (e.nodeA >= swNodeP.count || e.nodeB >= swNodeP.count) Fail("sidewalk edge node index out of range");
        g.sidewalkEdges.push_back(e);
    }

    g.RebuildIndices();
    return g;
}

std::vector<uint16_t> RoadGraph::Decode(uint16_t ref, const std::vector<uint16_t>& pool) const {
    std::vector<uint16_t> out;
    if (ref == 0xffff) return out;
    const size_t n = segments.size();
    if (ref < n) {
        out.push_back(ref);
        return out;
    }
    const size_t k = static_cast<size_t>(ref) - n;
    if (k >= pool.size()) return out;
    const size_t cnt = pool[k];
    for (size_t i = 0; i < cnt && k + 1 + i < pool.size(); ++i) out.push_back(pool[k + 1 + i]);
    return out;
}

size_t RoadGraph::InteriorPointCount(size_t seg) const {
    const size_t pts = shapePool.size() / 3;
    const size_t next = seg + 1 < segments.size() ? segments[seg + 1].shapeFirst : pts;
    const size_t first = segments[seg].shapeFirst;
    return next > first ? next - first : 0;
}

std::vector<RoadVec3> RoadGraph::SegmentPolyline(size_t seg) const {
    std::vector<RoadVec3> out;
    const RoadVec3& a = SegA(seg);
    out.push_back(a);
    const size_t ni = InteriorPointCount(seg);
    for (size_t k = 1; k <= ni; ++k) {
        const size_t t = static_cast<size_t>(segments[seg].shapeFirst) + k - 1;  // triple index
        if (t * 3 + 2 >= shapePool.size()) break;
        out.push_back({static_cast<float>(a.x + 0.0625 * shapePool[t * 3]),
                       static_cast<float>(a.y + 0.0625 * shapePool[t * 3 + 1]),
                       static_cast<float>(a.z + 0.0625 * shapePool[t * 3 + 2])});
    }
    out.push_back(SegB(seg));
    return out;
}

void RoadGraph::SegmentAabb(size_t seg, RoadVec3& mn, RoadVec3& mx) const {
    const RoadSegment& s = segments[seg];
    const RoadVec3& a = SegA(seg);
    const double base[3] = {a.x, a.y, a.z};
    float lo[3], hi[3];
    for (int k = 0; k < 3; ++k) {
        lo[k] = static_cast<float>(base[k] + static_cast<double>(static_cast<float>(s.aabbMin[k])) * 0.0625);
        hi[k] = static_cast<float>(base[k] + static_cast<double>(static_cast<float>(s.aabbMax[k])) * 0.0625);
    }
    mn = {lo[0], lo[1], lo[2]};
    mx = {hi[0], hi[1], hi[2]};
}

uint16_t RoadGraph::LaneChainLength(size_t seg, bool leftSide) const {
    uint16_t n = 1;
    size_t cur = seg;
    while (n < 64) {
        const uint16_t nx = leftSide ? segments[cur].left : segments[cur].right;
        if (nx == 0xffff) break;
        cur = nx;
        ++n;
    }
    return n;
}

void RoadGraph::RebuildIndices() {
    segTiles_.clear();
    edgeTiles_.clear();
    maxSegExtent_ = 0;
    maxEdgeExtent_ = 0;
    for (size_t i = 0; i < segments.size(); ++i) {
        const RoadVec3& a = SegA(i);
        segTiles_[RoadTileOf(a.x, a.z)].push_back(static_cast<uint32_t>(i));
        RoadVec3 mn, mx;
        SegmentAabb(i, mn, mx);
        maxSegExtent_ = std::max({maxSegExtent_, std::fabs(static_cast<double>(mn.x) - a.x), std::fabs(static_cast<double>(mx.x) - a.x),
                                  std::fabs(static_cast<double>(mn.z) - a.z), std::fabs(static_cast<double>(mx.z) - a.z)});
    }
    for (size_t i = 0; i < sidewalkEdges.size(); ++i) {
        const RoadVec3& a = sidewalkNodes[sidewalkEdges[i].nodeA].pos;
        const RoadVec3& b = sidewalkNodes[sidewalkEdges[i].nodeB].pos;
        edgeTiles_[RoadTileOf(a.x, a.z)].push_back(static_cast<uint32_t>(i));
        maxEdgeExtent_ = std::max({maxEdgeExtent_, std::fabs(static_cast<double>(b.x) - a.x), std::fabs(static_cast<double>(b.z) - a.z)});
    }
}

const std::vector<uint32_t>& RoadGraph::SegmentsInTile(RoadTile t) const {
    static const std::vector<uint32_t> kEmpty;
    auto it = segTiles_.find(t);
    return it == segTiles_.end() ? kEmpty : it->second;
}
const std::vector<uint32_t>& RoadGraph::SidewalkEdgesInTile(RoadTile t) const {
    static const std::vector<uint32_t> kEmpty;
    auto it = edgeTiles_.find(t);
    return it == edgeTiles_.end() ? kEmpty : it->second;
}

namespace {
// True when tile t's lattice cell, grown by `pad`, comes within `radius` of (x, z).
bool TileMayReach(RoadTile t, double x, double z, double radius, double pad) {
    const int odd = ((t.row % 2) + 2) % 2;
    const double cx = t.col * 320.0 - 4000.0 - 160.0 * odd;
    const double cz = 3640.0 - t.row * 280.0;
    return std::fabs(x - cx) <= 160.0 + pad + radius && std::fabs(z - cz) <= 140.0 + pad + radius;
}
}  // namespace

std::vector<uint32_t> RoadGraph::SegmentsNear(double x, double z, double radius) const {
    std::vector<uint32_t> out;
    for (const auto& kv : segTiles_) {
        if (!TileMayReach(kv.first, x, z, radius, maxSegExtent_)) continue;
        for (uint32_t i : kv.second) {
            RoadVec3 mn, mx;
            SegmentAabb(i, mn, mx);
            const double dx = std::max({mn.x - x, 0.0, x - mx.x});
            const double dz = std::max({mn.z - z, 0.0, z - mx.z});
            if (dx * dx + dz * dz <= radius * radius) out.push_back(i);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<uint32_t> RoadGraph::SidewalkEdgesNear(double x, double z, double radius) const {
    std::vector<uint32_t> out;
    for (const auto& kv : edgeTiles_) {
        if (!TileMayReach(kv.first, x, z, radius, maxEdgeExtent_)) continue;
        for (uint32_t i : kv.second) {
            const RoadVec3& a = sidewalkNodes[sidewalkEdges[i].nodeA].pos;
            const RoadVec3& b = sidewalkNodes[sidewalkEdges[i].nodeB].pos;
            const double ex = b.x - a.x, ez = b.z - a.z;
            const double l2 = ex * ex + ez * ez;
            double t = l2 > 1e-12 ? ((x - a.x) * ex + (z - a.z) * ez) / l2 : 0.0;
            t = std::clamp(t, 0.0, 1.0);
            const double px = a.x + ex * t - x, pz = a.z + ez * t - z;
            if (px * px + pz * pz <= radius * radius) out.push_back(i);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace sr3zone
