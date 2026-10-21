// Placement-record decode for the `~f<CCRR>.czh_pc` SR3Z records
// (spec-world-streaming.md Sec10.7.1, FINAL / CONFIRMED by disassembly +
// full-population data): name binding (+12 = byte offset into the same
// file's material-block name pool, target string `<mesh>.lmeshx`), the three
// Euler angles (+6, +8, +10 at 2^-12 rad; +10 is yaw), and the position
// (+0/+2/+4 at 2^-6 m plus the header origin).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "sr3zone/zone_header.h"

namespace sr3zone {

struct PlacedObject {
    bool bound = false;          // +12 hit a string start in the pool and it ends in ".lmeshx"
    std::string meshName;        // "<mesh>" (".lmeshx" stripped); empty when !bound
    std::array<float, 3> position{}; // world metres (origin + s16/64) - the mesh PIVOT
    std::array<float, 3> angles{};   // (a, b, c) = (+6, +8, +10) / 4096 rad; c is yaw
    // Row-major 3x3 per Sec10.7.1: rows are the world images of local X, Y, Z;
    // world = position + lx*r0 + ly*r1 + lz*r2 (row-vector convention).
    std::array<float, 9> rows{};
};

// Sec10.7.1 rotation builder 0x00da41c0: (a, b, c) rad -> row-major 3x3.
std::array<float, 9> PlacementRotationRows(float a, float b, float c);

// Transforms a local point: pos + p.x*r0 + p.y*r1 + p.z*r2.
std::array<float, 3> PlacementTransformPoint(const PlacedObject& o, const std::array<float, 3>& p);

// Name-pool lookup: `pool` is the raw pool bytes (file +0x20, length u32(file+4)).
// Returns true and fills meshName when `offset` is a string start (offset 0 or
// preceded by NUL) whose string ends in ".lmeshx".
bool BindPlacementName(const uint8_t* pool, size_t poolLen, uint16_t offset, std::string& meshName);

// Decodes one record given the header origin and pool.
PlacedObject DecodePlacement(const ZoneHeaderRecord& rec, const std::array<float, 3>& origin,
                             const uint8_t* pool, size_t poolLen);

// Whole-file convenience: parses `czh` (a `~f` .czh_pc) and decodes every record in file order.
// Throws what ZoneHeader::parse throws.
std::vector<PlacedObject> DecodeZonePlacements(vpp::ByteView czh);

} // namespace sr3zone
