#pragma once
// sr3airuntime/ped_navigator.h - the ambient ped sidewalk navigator of spec-ai-runtime.md Sec20.6.9.
//   (a) goal choice: the destination is the wander target of sub-state 33 (Sec20.2). CHOSEN here: a random sidewalk node
//       within kPedGoalRadiusChosenM of the ped, at least kPedGoalMinDistChosenM away (the spec gives no distribution).
//   (b) path: best-first search over sidewalk NODES (0x00a1d410). FINAL/HIGH: both ends of the start edge are seeded with
//       their straight-line distance (from the ped), edges cost their Euclidean length, costs are capped at 50 000 m.
//       CHOSEN: A* (Euclidean heuristic to the goal, which stays admissible) and an expansion cap of
//       kPedPathExpansionCapChosen nodes so one search is bounded.
//   (c) crossings: the PedCrossingGate (ped_crossing.h) is applied to the edge the ped is walking when it carries a
//       crossing; closed -> stop at the kerb and wait, open -> cross. Arrival: within 0.425 m (0.425^2 = 0.180625,
//       0x01117f10) of the target; after the final arrival event 8 is posted 1000 ms later (event8DueAtMs).
// Walking along the path: the ped steps toward the next route node; reaching it (<= 0.425 m) advances to the next. CHOSEN:
// the step is a straight line to the node (no lateral lane offset).
// CHOSEN (review PN-3): applying the 0.425 m radius to EVERY intermediate route node, not only the destination, is an
// extension. Sec20.6.9 (c) gives the 0.425 m radius for the target only; it does not say how intermediate nodes advance.
// Walk speed (Sec20.6.9(a), review PN-4): clip-driven, see kPedWalkPool* / PickPedWalkSpeed below.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <vector>

#include "sr3airuntime/near_field_ped_population.h"
#include "sr3airuntime/ped_crossing.h"
#include "sr3zone/road_graph.h"

namespace sr3airuntime {

constexpr double kPedArrivalRadiusM = 0.425;            // FINAL (0x01117f10 = 0.180625 = 0.425^2)
constexpr double kPedPathCostCapM = 50000.0;            // FINAL (0x0117835c)
constexpr int64_t kPedArrivalEventDelayMs = 1000;       // FINAL: event 8 posted with a 1000 ms delay
constexpr size_t kPedPathExpansionCapChosen = 6000;     // CHOSEN bound on nodes expanded per search
constexpr double kPedGoalRadiusChosenM = 80.0;          // CHOSEN goal-choice window
constexpr double kPedGoalMinDistChosenM = 10.0;         // CHOSEN
constexpr uint32_t kPedNoIndex = std::numeric_limits<uint32_t>::max();

struct PedRoute {
    uint32_t startEdge = kPedNoIndex;  // the edge the ped stands on
    std::vector<uint32_t> nodes;       // node sequence, nodes[0] = the start-edge end the ped walks to first
    std::vector<uint32_t> edges;       // edges[i] joins nodes[i] and nodes[i+1]
    double length = 0;                 // cost of the node path (without the seed leg)
};

inline bool PedArrived(const Vec3& pos, const Vec3& target) {
    const double dx = pos.x - target.x, dz = pos.z - target.z;
    return dx * dx + dz * dz <= kPedArrivalRadiusM * kPedArrivalRadiusM;
}

// Nearest sidewalk edge (distance to the segment) within `radius`, or kPedNoIndex.
uint32_t NearestSidewalkEdge(const sr3zone::RoadGraph& g, const Vec3& pos, double radius);

// Best-first path from the ped (standing on `startEdge`) to `goalNode`. nullopt when unreachable / capped.
// `expansionsOut` (optional) receives the number of nodes expanded.
std::optional<PedRoute> FindSidewalkRoute(const sr3zone::RoadGraph& g, const Vec3& pos, uint32_t startEdge,
                                          uint32_t goalNode, size_t* expansionsOut = nullptr);

// Goal choice (CHOSEN, see header). `rand01` yields uniform [0,1). kPedNoIndex when no candidate.
uint32_t ChoosePedGoalNode(const sr3zone::RoadGraph& g, const Vec3& pos, const std::function<double()>& rand01);

// Sec20.6.9(a) walk speeds (m/s, +0xa64 = 1.0 on ambient peds): base `walk` (state 26) male 1.386, female 1.368.
// The walk state is picked uniformly at random at human init among states 26-35 the anim set has (FINAL). The state ->
// clip -> speed mapping for variants 1..9 is NOT given (the table lists 5-6 variant clips for 12 variant columns).
// CHOSEN: the pick pool is the base walk plus each listed variant-clip speed, one entry each, uniform over the pool.
// CHOSEN: every pool entry is assumed present in the ped's anim set.
constexpr double kPedWalkPoolMale[] = {1.386, 1.849, 2.260, 2.023, 1.079, 1.506};
constexpr double kPedWalkPoolFemale[] = {1.368, 1.445, 1.550, 2.046, 0.697, 1.637, 1.500};
constexpr size_t kPedWalkPoolMaleSize = sizeof(kPedWalkPoolMale) / sizeof(double);
constexpr size_t kPedWalkPoolFemaleSize = sizeof(kPedWalkPoolFemale) / sizeof(double);

// Picks a ped's walk speed once, uniformly from its gender's pool. `rand01` yields uniform [0,1).
double PickPedWalkSpeed(bool female, const std::function<double()>& rand01);

struct PedNavigator {
    bool active = false;
    PedRoute route;
    size_t next = 0;       // index into route.nodes of the node being walked to
    Vec3 goal{};
    PedCrossingState crossing;
    int64_t event8DueAtMs = -1;  // set on arrival: arrival time + 1000 ms
    bool waiting = false;        // last step stopped at a kerb
    // counters (cumulative per ped)
    uint64_t edgesWalked = 0, waits = 0, crosses = 0;
};

enum class PedNavResult { Walking, Waiting, Arrived };

// Starts a route (search + goal). Returns false when no route exists.
bool PedNavigatorBegin(PedNavigator& nav, const sr3zone::RoadGraph& g, const Vec3& pos, uint32_t goalNode);

// One tick: moves `pos` toward the next route node by speed*dt. `crossingClosed(crossingIndex)` is the Sec20.6.13(h) test.
PedNavResult PedNavigatorStep(PedNavigator& nav, const sr3zone::RoadGraph& g, Vec3& pos, double dt, double speed,
                              int64_t nowMs, const std::function<bool(size_t)>& crossingClosed);

}  // namespace sr3airuntime
