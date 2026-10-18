#pragma once

#include "viewer_platform.h"
#include "sr3render/device.h"
#include "sr3render/mesh_renderer.h"
#include "sr3render/png_writer.h"
#include "sr3render/quad_renderer.h"
#include "sr3render/sun_shadow_map.h"
#include "sr3render/swap_chain.h"
#include "sr3render/tod_clock.h"
#include "sr3render/tod_lighting.h"
#include "sr3render/window.h"
#include "sr3render/texture_upload.h"
#include "sr3mesh/mesh_block.h"
#include "sr3rig/animated_pose.h"
#include "sr3rig/bone_palette.h"
#include "sr3rig/pose.h"
#include "sr3rig/rig.h"

namespace sr3viewer {

// One output image the multi-channel path is asked to produce: `skin` is
// exactly the per-bone Mat3x4 array bindSkin/poseSkin/skinMesh already are
// at each existing single-channel call site. `frames[0]` is always treated
// as the camera-defining pass, mirroring every existing single-channel
// command in this file (upload the FIRST pose, derive the shared camera
// from ITS bounds, reuse that same camera unmoved for every later frame).
struct MultiChannelFrame {
    std::string outPath;
    std::vector<sr3rig::Mat3x4> skin;
};

bool runMultiChannelFrames(sr3render::RenderDevice& device, const sr3mesh::MeshBlock& mesh,
                          size_t drawGroup, size_t rigBoneCount, uint32_t width, uint32_t height,
                          float yaw, float pitch, sr3render::MeshDrawMode mode,
                          const std::vector<MultiChannelFrame>& frames,
                          std::vector<std::vector<uint8_t>>* outPixels = nullptr);

} // namespace sr3viewer
