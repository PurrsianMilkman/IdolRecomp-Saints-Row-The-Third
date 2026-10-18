// sr3zone/road_graph.h - reader for the city's road graph (Traffic 0x2236)
// and sidewalk graph (Sidewalks 0x2238), spec-zone-data-format.md Sec13.2a
// (byte-complete, 2026-10-14). SUPERSEDES the earlier, partly wrong
// sr3zone/traffic_graph.h reading of 0x2236 (the spec's own correction
// note: header pair #8 is not a point pool, the 24-byte array in 0x2238 is
// nodes not segments, ...). traffic_graph.h is left untouched for its
// existing synthetic tests.
//
// WHERE THE DATA LIVES (Sec13.2a(a), FINAL): the whole city has ONE road
// graph and ONE sidewalk graph, both in `sr3_city~fawld.czn_pc`
// (awld_compact.str2_pc inside sr3_city_0.vpp_pc). The on-disk record is NOT
// split per cell. "Streaming" here therefore means: the graph is parsed once
// (about 2 MB of payload) when the archive holding it is loaded, and every
// record is bucketed by the Sec10.6(g) tile lattice so a caller can query
// only the tiles around the player (TilesNear / segments in those tiles).
//
// Coordinates are absolute world metres; no transform (Sec13.2a(a)).
//
// Tile rules (Sec13.2a(n)): a segment belongs to the tile of its node A;
// nodes and junctions to the tile of their own position; a sidewalk edge to
// the tile of its node A; a sidewalk node to the tile of its position.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "sr3zone/errors.h"
#include "vpp/byte_view.h"

namespace sr3zone {

struct RoadVec3 {
    float x = 0, y = 0, z = 0;
};

// Sec10.6(g) lattice: row = round((3640 - z) / 280),
// col = round((x + 4000 + 160 * (row mod 2)) / 320). FINAL.
struct RoadTile {
    int col = 0, row = 0;
    bool operator<(const RoadTile& o) const { return row != o.row ? row < o.row : col < o.col; }
    bool operator==(const RoadTile& o) const { return col == o.col && row == o.row; }
};
RoadTile RoadTileOf(double x, double z);

// Segment flag bits (Sec13.2a(d)). Only the bits with FINAL/HIGH meaning.
constexpr uint32_t kSegFlagOncomingLeft = 1u << 0;  // +0x3c neighbour runs the opposite way. FINAL
constexpr uint32_t kSegFlagNoSpawn3 = 1u << 3;      // excluded from spawning. HIGH (name OPEN)
constexpr uint32_t kSegFlagHighway = 1u << 4;       // Highway Only. FINAL
constexpr uint32_t kSegFlagNoSpawn8 = 1u << 8;      // excluded from spawning / runtime lists. HIGH
constexpr uint32_t kSegFlagStopLine = 1u << 7;      // stop line (Sec20.6.13(g)); NOT "signalised approach". FINAL
constexpr uint32_t kSegFlagAmbient = 1u << 10;      // Spline_Type "Ambient" class. FINAL
constexpr uint32_t kSegFlagOffroad = 1u << 16;      // FINAL
constexpr uint32_t kSegFlagBoat = 1u << 18;         // FINAL
// Spawner masks, Sec20.6.1(c): exclude 0x0080010c (bits 2,3,8,23).
constexpr uint32_t kSpawnExcludeMask = 0x0080010Cu;

// One lane, one direction: a cubic Bezier A -> B (Sec13.2a(c)).
struct RoadSegment {
    uint16_t nodeA = 0, nodeB = 0;
    RoadVec3 ctrl1, ctrl2;
    uint32_t flags = 0;
    uint16_t chord256 = 0;    // +0x20, chord length x256 mod 65536
    uint16_t halfArc256 = 0;  // +0x22
    std::array<int16_t, 3> aabbMin{}, aabbMax{};  // offsets from node A, x 1/16 m
    uint8_t signal = 0;       // +0x30 (1 proceed, 2 amber, 3 stop)
    uint16_t succRef = 0xffff, predRef = 0xffff, conflictRef = 0xffff;
    uint16_t right = 0xffff, left = 0xffff;  // lane neighbours; 0xffff = none
    uint16_t shapeFirst = 0;
};

// Signals (Sec13.2a(i), Sec20.6.13). Records as stored; runtime fields hold their on-disk values.
struct SignalLight {           // 16 B, header +0x48
    uint64_t id = 0;           // +0x00 identifier matched against the 0x2241 light records
    int32_t objectIndex = -1;  // +0x08 runtime
    uint16_t zoneId = 0xffff;  // +0x0c runtime
    uint8_t vehicleState = 3;  // +0x0e
    uint8_t pedState = 2;      // +0x0f
};
struct SignalJunction {        // 20 B, header +0x50
    RoadVec3 pos;              // +0x00
    uint16_t firstPhase = 0;   // +0x0c
    uint8_t currentPhase = 0;  // +0x0e runtime
    uint8_t byte0f = 0;        // +0x0f (meaning PARTIAL)
    int32_t timer = -1;        // +0x10 runtime
};
struct SignalPhase {           // 6 B, header +0x58
    uint16_t firstLight = 0;   // +0x00 first index in the light list
    uint16_t firstLane = 0;    // +0x02 first index in the lane list
    uint16_t duration = 0;     // +0x04 milliseconds
};

struct SidewalkNode {
    RoadVec3 pos;
    uint8_t byte0c = 0xff, byte0d = 0;
    uint16_t adjFirst = 0;
};
struct SidewalkEdge {
    uint16_t nodeA = 0, nodeB = 0;
    uint16_t width256 = 0;      // walkway width x256
    uint16_t crossing = 0xffff;  // crossing index; 0xffff = none
    double width() const { return width256 / 256.0; }
};

class RoadGraph {
public:
    // Locates the 0x2236 and 0x2238 sections in a whole `.czn_pc` file and
    // decodes both. Throws FormatError when either is missing or fails a
    // structural check (array outside the payload, a link out of range).
    static RoadGraph Parse(vpp::ByteView cznContent);

    std::vector<RoadVec3> nodes;
    std::vector<RoadSegment> segments;
    std::vector<int16_t> shapePool;  // 3 x s16 per point (Sec13.2a(f))
    std::vector<uint16_t> succPool, predPool, conflictPool;  // raw pools: {n, n x idx}

    std::vector<SidewalkNode> sidewalkNodes;
    std::vector<uint16_t> sidewalkAdj;  // CSR node -> incident edge lists
    std::vector<SidewalkEdge> sidewalkEdges;

    // ---- signals (Sec13.2a(i)) ----
    std::vector<SignalLight> lights;
    std::vector<SignalJunction> junctions;
    std::vector<SignalPhase> phases;     // global array; a junction's phases are contiguous from firstPhase
    std::vector<uint16_t> phaseLights;   // header +0x60: light indices
    std::vector<uint16_t> phaseLanes;    // header +0x68: segment indices
    std::vector<int32_t> crossingLight;  // per sidewalk crossing: light index, -1 = no light (Sec13.2a(l))

    // Phase count of junction j: next junction's firstPhase minus this one's (phase total for the last).
    size_t PhaseCount(size_t j) const {
        const size_t end = j + 1 < junctions.size() ? junctions[j + 1].firstPhase : phases.size();
        return end - junctions[j].firstPhase;
    }
    // List extents of a (global) phase: up to the next phase's first index, or the pool size.
    size_t PhaseLightCount(size_t p) const {
        const size_t end = p + 1 < phases.size() ? phases[p + 1].firstLight : phaseLights.size();
        return end - phases[p].firstLight;
    }
    size_t PhaseLaneCount(size_t p) const {
        const size_t end = p + 1 < phases.size() ? phases[p + 1].firstLane : phaseLanes.size();
        return end - phases[p].firstLane;
    }

    // ---- link decoding (Sec13.2a(e)) ----
    std::vector<uint16_t> Successors(size_t seg) const { return Decode(segments[seg].succRef, succPool); }
    std::vector<uint16_t> Predecessors(size_t seg) const { return Decode(segments[seg].predRef, predPool); }
    std::vector<uint16_t> Conflicts(size_t seg) const { return Decode(segments[seg].conflictRef, conflictPool); }

    // ---- geometry ----
    const RoadVec3& SegA(size_t seg) const { return nodes[segments[seg].nodeA]; }
    const RoadVec3& SegB(size_t seg) const { return nodes[segments[seg].nodeB]; }
    // Polyline of a segment as the engine's point accessor yields it:
    // node A, interior shape points (nodeA + 0.0625 * s16 triple), node B.
    std::vector<RoadVec3> SegmentPolyline(size_t seg) const;
    // Interior-point count of segment `seg` (Sec13.2a(f)).
    size_t InteriorPointCount(size_t seg) const;
    // Absolute AABB decoded with the Sec13.2a(c) codec.
    void SegmentAabb(size_t seg, RoadVec3& mn, RoadVec3& mx) const;
    // Lane speed limit would need traffic_lanes.xtbl; not stored here.
    uint16_t LaneChainLength(size_t seg, bool leftSide) const;

    // ---- per-tile indices, built by Parse ----
    const std::vector<uint32_t>& SegmentsInTile(RoadTile t) const;
    const std::vector<uint32_t>& SidewalkEdgesInTile(RoadTile t) const;
    // All segments / edges whose tile (or any of the 8 neighbouring tiles,
    // since a segment can be longer than a tile) is within `radius` of
    // (x,z); callers still apply their own exact test.
    std::vector<uint32_t> SegmentsNear(double x, double z, double radius) const;
    std::vector<uint32_t> SidewalkEdgesNear(double x, double z, double radius) const;
    // Rebuilds the per-tile indices; Parse calls it, synthetic builders call it after filling the arrays.
    void RebuildIndices();
    size_t TileCountSegments() const { return segTiles_.size(); }
    size_t TileCountSidewalk() const { return edgeTiles_.size(); }

private:
    std::vector<uint16_t> Decode(uint16_t ref, const std::vector<uint16_t>& pool) const;

    std::map<RoadTile, std::vector<uint32_t>> segTiles_, edgeTiles_;
    double maxSegExtent_ = 0, maxEdgeExtent_ = 0;  // largest reach beyond the owning tile's node A
    friend struct RoadGraphBuilder;
};

}  // namespace sr3zone
