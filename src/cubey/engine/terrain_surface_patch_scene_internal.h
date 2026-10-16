#pragma once

#include <cubey/engine/terrain_surface_patch_scene.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/texture.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/sampler.h>

#include <limits>
#include <type_traits>

namespace cubey {
namespace terrain_surface_detail {
inline constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
struct DaylightPush {
    math::Vec4 camera_position_radius, radii_ground, rayleigh, mie, ozone;
    math::Vec4 sun_direction_radius, atmosphere_options;
};

struct GlobalResources {
    std::optional<render::GeneratedPbrEnvironment> environment;
    std::optional<render::TextureCube> terrain_diffuse;
    std::optional<render::Texture2D> detail;
    std::optional<vulkan::Sampler> linear_sampler, nearest_sampler;
    std::optional<render::FrameUniformMaterialInstance<render::TerrainSurfacePatchUniforms>> frame;
    std::optional<vulkan::GpuTimestampProfiler> profiler;
    std::optional<vulkan::GpuTimestampProfiler> context_profiler;
    std::vector<bool> context_profile_slots;
    std::optional<vulkan::Buffer> visual_flow;
    std::optional<vulkan::DescriptorSetBundle> visual_flow_set;
    std::optional<render::ComputePipelineResource> visual_flow_pipeline;
    double previous_clock_s = 0.0;
    bool flow_initialized = false;
    struct WhitewaterSlot {
        std::optional<vulkan::Buffer> buffer;
        std::unique_ptr<vulkan::DescriptorSetBundle> descriptors;
        std::uint64_t generation = std::numeric_limits<std::uint64_t>::max();
    };
    std::vector<WhitewaterSlot> whitewater_slots;
    math::Mat4 shadow_matrix{1.0F};
    math::Mat4 reported_shadow_matrix{0.0F};
    std::uint32_t reported_shadow_extent = 0U;
    float terrain_low = 0.0F, terrain_high = 0.0F, domain_m = 0.0F;
    std::uint32_t frame_slot_count = 0U;
};
struct TargetResources {
    std::unique_ptr<render::TerrainSurfacePatchRenderer> patch;
    std::unique_ptr<render::GraphicsPipelineResource> sky, copy, display;
    std::unique_ptr<render::GraphicsPipelineResource> foreground_merge;
    std::vector<std::unique_ptr<vulkan::DescriptorSetBundle>> foreground_sets;
    std::unique_ptr<render::GraphicsPipelineResource> rain;
    std::unique_ptr<render::GraphicsPipelineResource> whitewater;
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
};
struct DaylightResources {
    std::optional<vulkan::Buffer> captured_daylight;
    std::vector<std::unique_ptr<vulkan::DescriptorSetBundle>> daylight_sets;
    std::unique_ptr<render::ComputePipelineResource> daylight_pipeline;
    bool daylight_initialized = false;
};
struct SurfaceResources {
    std::optional<render::Texture2D> landform_surface, climate_surface;
    std::optional<render::Texture2D> correlated_surface;
};
static_assert(std::is_nothrow_move_assignable_v<TargetResources>);
static_assert(std::is_nothrow_move_assignable_v<DaylightResources>);
static_assert(std::is_nothrow_move_assignable_v<SurfaceResources>);
} // namespace terrain_surface_detail

struct TerrainSurfacePatchScene::State : terrain_surface_detail::GlobalResources,
                                         terrain_surface_detail::TargetResources,
                                         terrain_surface_detail::DaylightResources,
                                         terrain_surface_detail::SurfaceResources {};
} // namespace cubey
