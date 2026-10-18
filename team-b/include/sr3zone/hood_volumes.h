// sr3zone/hood_volumes.h - hood volumes and their spawn categories,
// spec-ai-runtime.md Sec20.6.8 (FINAL). The hoods are `Hood` objects in the
// city's always-loaded zone sr3_city~fawld.czn_pc (Objects section 0x2234);
// each carries `hood_spawn_category` (string), `hood_alt_spawn_category`
// (string) and `hood_bsp_tree` (a blob: the volume).
//
// Volume test (Sec20.6.8, 0x00dcda30): blob = u32 planeCount, u32 nodeCount,
// 8 runtime bytes, planes[planeCount] {float nx,ny,nz,d}, nodes[nodeCount]
// {u16 plane, u16 childIfBehind, u16 childIfFront}. Walk from node 0:
// s = n.p + d; s < -0.0001 -> childIfBehind (0 = outside), else childIfFront
// (0 = inside). y is up; absolute world metres.
//
// Lookup: first hood, in object-list order, whose volume contains the point
// wins. List order = the Objects section's record order here (CHOSEN: the
// engine's world-object-list order is load order, not traced further). No hood
// -> the caller falls back to "Default Spawn".
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vpp/byte_view.h"

namespace sr3zone {

struct HoodVolume {
    struct Plane {
        float nx = 0, ny = 0, nz = 0, d = 0;
    };
    struct Node {
        uint16_t plane = 0, behind = 0, front = 0;
    };
    std::string name;               // ctg_object_name, e.g. "HOOD_DT_01"
    std::string spawnCategory;      // hood_spawn_category
    std::string altSpawnCategory;   // hood_alt_spawn_category (empty when absent)
    std::vector<Plane> planes;
    std::vector<Node> nodes;

    // Sec20.6.8 volume test. An empty tree contains nothing.
    bool Contains(double x, double y, double z) const;
};

// Every object of `cznContent` carrying a parseable `hood_bsp_tree`, in
// Objects-section record order. Empty if the file has no Objects section.
std::vector<HoodVolume> LoadHoodVolumes(vpp::ByteView cznContent);

// First hood containing the point, or nullptr.
const HoodVolume* FindHood(const std::vector<HoodVolume>& hoods, double x, double y, double z);

}  // namespace sr3zone
