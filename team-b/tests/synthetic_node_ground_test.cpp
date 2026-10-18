// Unit test for the CHOSEN ground stand-in (include/sr3player/node_ground.h); see
// results/freeroam_ground_height_20261018.md.
#include <cstdio>

#include "sr3player/node_ground.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("CHECK FAILED: %s (line %d)\n", #c, __LINE__); ++g_fail; } } while (0)

int main() {
    using namespace sr3player;
    CHECK(!HeightmapValueHasData(-5.0f));
    CHECK(HeightmapValueHasData(0.0f));
    CHECK(HeightmapValueHasData(-4.0f));
    CHECK(HeightmapValueHasData(32.0f));

    NodeGroundIndex idx;
    double y = 0;
    CHECK(!idx.Nearest(0, 0, 40, 0, y));  // empty
    idx.Add(-1272.0f, 32.15f, -584.4f);
    idx.Add(-1311.1f, 28.75f, -544.6f);
    idx.Add(-1266.9f, 32.13f, -584.4f);
    // spawn (-1280,-560): nearest is the first node (25.7 m)
    CHECK(idx.Nearest(-1280, -560, 40, 0, y) && std::fabs(y - 32.15) < 1e-4);
    // beyond 40 m of everything
    CHECK(!idx.Nearest(-1280, -400, 40, 0, y));
    // exactly near the second node
    CHECK(idx.Nearest(-1311, -544, 40, 0, y) && std::fabs(y - 28.75) < 1e-4);
    // overpass tie-break: two nodes at ~same XZ, different Y; referenceY picks
    NodeGroundIndex ov;
    ov.Add(10, 5.0f, 10);
    ov.Add(10.2f, 25.0f, 10);
    CHECK(ov.Nearest(10, 10, 40, 24.0, y) && std::fabs(y - 25.0) < 1e-4);
    CHECK(ov.Nearest(10, 10, 40, 4.0, y) && std::fabs(y - 5.0) < 1e-4);
    // cell boundaries / negative coordinates
    NodeGroundIndex nb;
    nb.Add(-0.5f, 7.0f, -0.5f);
    CHECK(nb.Nearest(0.5, 0.5, 40, 0, y) && std::fabs(y - 7.0) < 1e-4);
    CHECK(BoundsContainXZ(-22.3, 22.3, -10.4, 13.0, 0, 0));
    CHECK(!BoundsContainXZ(-22.3, 22.3, -10.4, 13.0, -1280, -560));
    if (g_fail) return 1;
    std::printf("synthetic_node_ground_test OK\n");
    return 0;
}
