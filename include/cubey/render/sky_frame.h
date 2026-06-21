#pragma once

#include <cubey/core/math.h>
#include <cubey/render/atmosphere_background_frame.h>
#include <cubey/render/celestial_system.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/material.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/target.h>
#include <cubey/render/view_ray_basis_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <span>

namespace cubey::render {

struct SkyFrameUniforms {
    cubey::math::Vec4 camera_right_aspect;
    cubey::math::Vec4 camera_up_tan_half_fovy;
    cubey::math::Vec4 camera_forward_enabled;
    cubey::math::Vec4 sun_direction_radius;
    cubey::math::Vec4 moon_direction_radius;
    cubey::math::Vec4 sun_color_intensity;
    cubey::math::Vec4 sun_disk_glow;
    cubey::math::Vec4 camera_position_radius;
    cubey::math::Vec4 background_space_limb;
    cubey::math::Vec4 atmosphere_mode_options;
    cubey::math::Vec4 night_options;
    cubey::math::Vec4 celestial_options;
    cubey::math::Vec4 moon_options;
    cubey::math::Vec4 moon_phase_options;
    cubey::math::Vec4 milky_way_options;
    cubey::math::Vec4 render_options;
};

static_assert(sizeof(SkyFrameUniforms) == sizeof(float) * 64U);

struct SkyFrameUniformInputs {
    ViewRayBasis3D view_rays{};
    cubey::math::Vec3 camera_position_m{0.0F, 0.0F, 0.0F};
    float planet_radius_m = 1.0F;
    float atmosphere_outer_radius_m = 1.0F;
    std::uint32_t atmosphere_mode = 1U;
    float moon_angular_radius_scale = 1.0F;
};

struct SkyFrameMaterialConfig {
    std::uint32_t frame_slot_count = 1;
    AtmosphereBackgroundTextureBindings textures{};
};

struct SkyFramePipelineConfig {
    VkExtent2D extent{};
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    std::span<const ShaderStageFile> shader_stage_files{};
};

[[nodiscard]] SkyFrameUniforms sky_frame_uniforms(const CelestialSystem& celestial,
                                                  const SkyFrameUniformInputs& inputs);
[[nodiscard]] MaterialPassInfo sky_pass_info();

class SkyFrame {
  public:
    SkyFrame() = default;

    SkyFrame(const SkyFrame&) = delete;
    SkyFrame& operator=(const SkyFrame&) = delete;
    SkyFrame(SkyFrame&&) = delete;
    SkyFrame& operator=(SkyFrame&&) = delete;

    void create_materials(const cubey::vulkan::Device& device, const SkyFrameMaterialConfig& config);
    void create_pipeline(const cubey::vulkan::Device& device, const SkyFramePipelineConfig& config);
    void destroy_pipeline();
    void destroy();

    void upload(FrameSlot frame_slot, const SkyFrameUniforms& uniforms) const;
    void record_pass(const cubey::vulkan::CommandRecorder& recorder, ColorTargetView target,
                     FrameSlot frame_slot) const;

    [[nodiscard]] bool materials_created() const noexcept;
    [[nodiscard]] const FrameUniformMaterialInstance<SkyFrameUniforms>& material() const;
    [[nodiscard]] const GraphicsPipelineResource& pipeline() const;

  private:
    std::optional<FrameUniformMaterialInstance<SkyFrameUniforms>> material_;
    std::optional<GraphicsPipelineResource> pipeline_;
};

} // namespace cubey::render
