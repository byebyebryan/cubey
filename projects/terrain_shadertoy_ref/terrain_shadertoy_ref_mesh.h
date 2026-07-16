#pragma once

#include "terrain_shadertoy_ref_config.h"

#include <cubey/render/frame_data.h>
#include <cubey/render/target.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>

namespace cubey::vulkan {
class Device;
class GpuRuntime;
class GpuTimestampProfiler;
} // namespace cubey::vulkan

namespace cubey::render {
class Texture2D;
} // namespace cubey::render

namespace cubey::projects::terrain_shadertoy_ref {

class ReferenceMeshRenderer {
  public:
    ReferenceMeshRenderer();
    ~ReferenceMeshRenderer();

    ReferenceMeshRenderer(const ReferenceMeshRenderer&) = delete;
    ReferenceMeshRenderer& operator=(const ReferenceMeshRenderer&) = delete;

    void create_global_resources(cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                                 const TerrainShadertoyRefConfig& config,
                                 VkExtent2D reference_extent,
                                 const cubey::render::Texture2D& channel_texture,
                                 const cubey::render::Texture2D& control_texture);
    void create_frame_resources(cubey::vulkan::Device& device, VkFormat color_format,
                                VkExtent2D extent, std::uint32_t frame_slot_count);
    void destroy_frame_resources();
    void destroy_global_resources();

    [[nodiscard]] float inspection_focus_distance() const;
    void set_inspection_orbit(float yaw_radians, float pitch_radians, float distance);

    void record(VkCommandBuffer command_buffer, const cubey::render::ColorTargetView& color_target,
                cubey::render::FrameSlot frame_slot, bool present,
                cubey::vulkan::GpuTimestampProfiler* profiler);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cubey::projects::terrain_shadertoy_ref
