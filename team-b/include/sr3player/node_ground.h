// CHOSEN ground stand-in (2026-10-18). See results/freeroam_ground_height_20261018.md for the measurements and the
// four open Team A questions this stands in for. NOT an engine fact: the spec gives no walkable-ground source for
// tiles like 0915 (spec-zone-data-format.md 13.2 / 13.5), so:
//  1. a 0x2244 heightmap value of exactly -5 is treated as "no data" (13.5 itself flags -5 as colliding with the
//     out-of-bounds sentinel; measured: the `~f` records read -5 over land);
//  2. when neither the real 0x2237 mesh nor the heightmap has data, the nearest road/sidewalk graph node's Y
//     (13.2a: absolute world metres, within ~0.07 m of placed objects) within kNodeGroundMaxDistance is used.
// Header-only, pure (no I/O), unit-tested by tests/synthetic_node_ground_test.cpp.
#pragma once

#include <cmath>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace sr3player {

constexpr float kHeightmapNoDataValue = -5.0f;     // CHOSEN: treated as "no data" (Team A question 4)
constexpr double kNodeGroundMaxDistance = 40.0;    // CHOSEN radius, metres

inline bool HeightmapValueHasData(float h) { return h != kHeightmapNoDataValue; }

struct GroundNode {
    float x = 0, y = 0, z = 0;
};

// Uniform-grid nearest-node index (cell 40 m). Build once, query per sample.
class NodeGroundIndex {
public:
    void Add(float x, float y, float z) { nodes_.push_back({x, y, z}); built_ = false; }
    size_t size() const { return nodes_.size(); }

    // Nearest node within maxDist of (x,z) in the XZ plane. When several candidate nodes lie at similar distance and
    // `referenceY` is finite, the one whose Y is closest to referenceY breaks ties within 1 m of the best distance
    // (overpass/underpass disambiguation, same convention as the 0x2237 mesh sampler). Returns false if none.
    bool Nearest(double x, double z, double maxDist, double referenceY, double& outY) const {
        if (nodes_.empty()) return false;
        if (!built_) Build();
        const int r = static_cast<int>(std::ceil(maxDist / kCell));
        const int cx = Cell(x), cz = Cell(z);
        double bestD = 1e30;
        double bestY = 0;
        bool found = false;
        for (int ix = cx - r; ix <= cx + r; ++ix)
            for (int iz = cz - r; iz <= cz + r; ++iz) {
                auto it = grid_.find(Key(ix, iz));
                if (it == grid_.end()) continue;
                for (size_t i : it->second) {
                    const double d = std::hypot(nodes_[i].x - x, nodes_[i].z - z);
                    if (d > maxDist) continue;
                    if (!found || d < bestD - 1.0) {
                        bestD = d; bestY = nodes_[i].y; found = true;
                    } else if (d <= bestD + 1.0 && std::fabs(nodes_[i].y - referenceY) < std::fabs(bestY - referenceY)) {
                        bestD = (d < bestD ? d : bestD); bestY = nodes_[i].y;
                    }
                }
            }
        if (found) outY = bestY;
        return found;
    }

private:
    static constexpr double kCell = 40.0;
    static int Cell(double v) { return static_cast<int>(std::floor(v / kCell)); }
    static long long Key(int a, int b) { return (static_cast<long long>(a) << 32) ^ static_cast<unsigned int>(b); }
    void Build() const {
        grid_.clear();
        for (size_t i = 0; i < nodes_.size(); ++i) grid_[Key(Cell(nodes_[i].x), Cell(nodes_[i].z))].push_back(i);
        built_ = true;
    }
    std::vector<GroundNode> nodes_;
    mutable std::unordered_map<long long, std::vector<size_t>> grid_;
    mutable bool built_ = false;
};

// Does the XZ box [minX,maxX]x[minZ,maxZ] contain (x,z)? (CHOSEN rule 3: ignore ground tiles whose own bounds do not
// contain the spawn when measuring finalGroundY.)
inline bool BoundsContainXZ(double minX, double maxX, double minZ, double maxZ, double x, double z) {
    return x >= minX && x <= maxX && z >= minZ && z <= maxZ;
}

}  // namespace sr3player
