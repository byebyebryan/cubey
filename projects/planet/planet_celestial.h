#pragma once

#include <cubey/core/math.h>
#include <cubey/render/atmosphere_environment.h>
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

namespace cubey::projects::planet {

struct PlanetCelestialSun {
    bool visible = true;
    cubey::math::Vec3 direction{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 color{1.0F, 0.94F, 0.82F};
    float intensity = 1.0F;
    float angular_radius_rad = 0.004675F;
};

struct PlanetCelestialSystem {
    PlanetCelestialSun sun{};
};

struct PlanetCelestialFrameUniforms {
    cubey::math::Vec4 camera_right_aspect;
    cubey::math::Vec4 camera_up_tan_half_fovy;
    cubey::math::Vec4 camera_forward_enabled;
    cubey::math::Vec4 sun_direction_radius;
    cubey::math::Vec4 sun_color_intensity;
    cubey::math::Vec4 sun_disk_glow;
};

static_assert(sizeof(PlanetCelestialFrameUniforms) == sizeof(float) * 24U);

struct PlanetCelestialFrameUniformInputs {
    cubey::render::ViewRayBasis3D view_rays{};
};

struct PlanetCelestialFrameMaterialConfig {
    std::uint32_t frame_slot_count = 1;
};

struct PlanetCelestialFramePipelineConfig {
    VkExtent2D extent{};
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    std::span<const cubey::render::ShaderStageFile> shader_stage_files{};
};

[[nodiscard]] PlanetCelestialSystem planet_celestial_system_from_atmosphere(
    const cubey::render::AtmosphereEnvironmentConfig& atmosphere);
[[nodiscard]] cubey::render::AtmosphereEnvironmentConfig planet_atmosphere_inputs_from_celestial(
    cubey::render::AtmosphereEnvironmentConfig atmosphere,
    const PlanetCelestialSystem& celestial);
[[nodiscard]] cubey::render::AtmosphereEnvironmentLighting planet_celestial_lighting(
    const cubey::render::AtmosphereEnvironmentConfig& atmosphere,
    const PlanetCelestialSystem& celestial);
[[nodiscard]] PlanetCelestialFrameUniforms planet_celestial_frame_uniforms(
    const PlanetCelestialSystem& celestial, const PlanetCelestialFrameUniformInputs& inputs);
[[nodiscard]] cubey::render::MaterialPassInfo planet_celestial_pass_info();

class PlanetCelestialFrame {
  public:
    PlanetCelestialFrame() = default;

    PlanetCelestialFrame(const PlanetCelestialFrame&) = delete;
    PlanetCelestialFrame& operator=(const PlanetCelestialFrame&) = delete;
    PlanetCelestialFrame(PlanetCelestialFrame&&) = delete;
    PlanetCelestialFrame& operator=(PlanetCelestialFrame&&) = delete;

    void create_materials(const cubey::vulkan::Device& device,
                          const PlanetCelestialFrameMaterialConfig& config);
    void create_pipeline(const cubey::vulkan::Device& device,
                         const PlanetCelestialFramePipelineConfig& config);
    void destroy_pipeline();
    void destroy();

    void upload(cubey::render::FrameSlot frame_slot,
                const PlanetCelestialFrameUniforms& uniforms) const;
    void record_pass(const cubey::vulkan::CommandRecorder& recorder,
                     cubey::render::ColorTargetView target,
                     cubey::render::FrameSlot frame_slot) const;

    [[nodiscard]] bool materials_created() const noexcept;
    [[nodiscard]] const cubey::render::FrameUniformMaterialInstance<PlanetCelestialFrameUniforms>&
    material() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource& pipeline() const;

  private:
    std::optional<cubey::render::FrameUniformMaterialInstance<PlanetCelestialFrameUniforms>>
        material_;
    std::optional<cubey::render::GraphicsPipelineResource> pipeline_;
};

} // namespace cubey::projects::planet
