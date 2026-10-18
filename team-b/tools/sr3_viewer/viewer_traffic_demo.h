#pragma once

#include "viewer_platform.h"
#include "sr3zone/zone_geometry.h"
#include "sr3zone/zone_header.h"
#include "sr3zone/zone_objects.h"
#include "sr3zone/traffic_graph.h"
#include "sr3airuntime/traffic_following.h"
#include "sr3airuntime/traffic_signals.h"
#include "sr3airuntime/ped_crossing.h"
#include "sr3airuntime/ped_navigator.h"
#include "sr3airuntime/traffic_lane_follower.h"
#include "sr3airuntime/road_graph_placement.h"
#include "sr3zone/hood_volumes.h"
#include "sr3zone/road_graph.h"
#include "sr3zone/zone_heightmap.h"
#include "sr3airuntime/ambient_population.h"
#include "sr3airuntime/near_field_vehicle_population.h"
#include "sr3airuntime/freeroam_population.h"
#include "sr3airuntime/police_roadblock_spawn.h"
#include "sr3airuntime/near_field_ped_population.h"
#include "sr3airuntime/npc_ai_runtime.h"
#include "sr3luahost/engine_state.h"

namespace sr3viewer {
sr3zone::TrafficRecord BuildPlaytestTrafficLaneGraphFixture(float ax, float az, std::vector<uint8_t>& outBuf);

size_t NearestLaneGraphSegmentStart(const sr3airuntime::TrafficLaneGraph& graph, const sr3airuntime::Vec3& pos);

std::optional<sr3airuntime::OvertakeCandidate> ResolveNearestNearFieldVehicleAhead(
    const std::vector<sr3airuntime::NearFieldVehicle>& liveVehicles, uint64_t selfId, double selfX, double selfZ,
    double selfHeadingRad, const std::unordered_map<uint64_t, double>& headingById,
    const std::unordered_map<uint64_t, double>& speedById);

} // namespace sr3viewer
