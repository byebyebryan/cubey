#pragma once

#include "planet_config.h"

#include <cubey/core/math.h>
#include <cubey/core/run_config.h>
#include <cubey/render/celestial_system.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/material.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/mesh.h>
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

inline constexpr float kPlanetMeanSolarDayHours = cubey::render::kCelestialMeanSolarDayHours;
inline constexpr float kPlanetMeanTropicalYearDays = cubey::render::kCelestialMeanTropicalYearDays;
inline constexpr float kPlanetEarthSiderealRotationHours =
    cubey::render::kCelestialEarthSiderealRotationHours;
inline constexpr float kPlanetEarthSiderealRotationPeriodDays =
    cubey::render::kCelestialEarthSiderealRotationPeriodDays;
inline constexpr float kPlanetMoonSiderealOrbitPeriodDays =
    cubey::render::kCelestialMoonSiderealOrbitPeriodDays;
inline constexpr float kPlanetMoonOrbitInclinationRad =
    cubey::render::kCelestialMoonOrbitInclinationRad;
inline constexpr float kPlanetDefaultMoonOrbitPhaseOffsetCycles =
    cubey::render::kCelestialDefaultMoonOrbitPhaseOffsetCycles;
inline constexpr float kPlanetFullMoonLightIntensity =
    cubey::render::kCelestialFullMoonLightIntensity;
inline constexpr float kPlanetDefaultDaylightExposure =
    cubey::render::kCelestialDefaultDaylightExposure;
inline constexpr float kPlanetDefaultTwilightExposure =
    cubey::render::kCelestialDefaultTwilightExposure;
inline constexpr float kPlanetDefaultNightExposure = cubey::render::kCelestialDefaultNightExposure;

using PlanetExposureConfig = cubey::render::CelestialExposureConfig;
using PlanetExposureView = cubey::render::CelestialExposureView;
using PlanetCelestialBodyType = cubey::render::CelestialBodyType;
using PlanetCelestialSun = cubey::render::CelestialSun;
using PlanetCelestialMoon = cubey::render::CelestialMoon;
using PlanetCelestialBody = cubey::render::CelestialBody;
using PlanetCelestialSystem = cubey::render::CelestialSystem;
using PlanetCelestialDiagnostics = cubey::render::CelestialDiagnostics;
using PlanetCelestialBodyRenderPlacementInputs =
    cubey::render::CelestialBodyRenderPlacementInputs;
using PlanetCelestialBodyRenderPlacement = cubey::render::CelestialBodyRenderPlacement;
using PlanetSolarTime = cubey::render::CelestialSolarTime;

[[nodiscard]] PlanetSolarTime planet_solar_time_from_run_config(const RunConfig& config);
[[nodiscard]] PlanetExposureConfig planet_exposure_config_from_run_config(const RunConfig& config);

using PlanetSolarSystemConfig = cubey::render::CelestialSolarSystemConfig;

struct PlanetSkyFrameUniforms {
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
};

static_assert(sizeof(PlanetSkyFrameUniforms) == sizeof(float) * 40U);

struct PlanetCelestialBodyFrameUniforms {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec4 center_radius{0.0F, 0.0F, 0.0F, 0.0F};
    cubey::math::Vec4 camera_position_options{0.0F, 0.0F, 0.0F, 0.35F};
    cubey::math::Vec4 light_direction_intensity{0.0F, 1.0F, 0.0F, 1.0F};
    cubey::math::Vec4 color_phase{0.58F, 0.62F, 0.74F, 0.5F};
    cubey::math::Vec4 visibility_atmosphere{1.0F, 0.0F, 0.0F, 0.0F};
};

static_assert(sizeof(PlanetCelestialBodyFrameUniforms) == sizeof(float) * 36U);

struct PlanetSkyFrameUniformInputs {
    cubey::render::ViewRayBasis3D view_rays{};
    cubey::math::Vec3 camera_position_m{0.0F, 0.0F, 0.0F};
    float planet_radius_m = 1.0F;
    float atmosphere_outer_radius_m = 1.0F;
    PlanetAtmosphereMode atmosphere_mode = PlanetAtmosphereMode::Physical;
    float moon_angular_radius_scale = 1.0F;
};

struct PlanetCelestialBodyAtmosphereInputs {
    cubey::math::Vec3 camera_position_m{0.0F, 0.0F, 0.0F};
    float planet_radius_m = 0.0F;
    float atmosphere_outer_radius_m = 0.0F;
};

using PlanetCelestialLighting = cubey::render::CelestialLighting;

struct PlanetCelestialBodyFrameInputs {
    cubey::math::Vec3 camera_render_position_m{0.0F, 0.0F, 0.0F};
    PlanetCelestialBodyAtmosphereInputs atmosphere{};
};

using PlanetAtmosphereInputs = cubey::render::CelestialAtmosphereInputs;

struct PlanetSkyFrameMaterialConfig {
    std::uint32_t frame_slot_count = 1;
};

struct PlanetSkyFramePipelineConfig {
    VkExtent2D extent{};
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    std::span<const cubey::render::ShaderStageFile> shader_stage_files{};
};

struct PlanetCelestialBodyFrameMaterialConfig {
    std::uint32_t frame_slot_count = 1;
};

struct PlanetCelestialBodyFramePipelineConfig {
    VkExtent2D extent{};
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    std::span<const cubey::render::ShaderStageFile> shader_stage_files{};
};

[[nodiscard]] float planet_solar_time_simulation_day(const PlanetSolarTime& time);
void planet_solar_time_advance(PlanetSolarTime& time, double delta_seconds);
[[nodiscard]] float planet_celestial_synodic_month_days(const PlanetSolarSystemConfig& solar = {});
[[nodiscard]] PlanetCelestialSystem
planet_celestial_system_from_solar_time(const PlanetSolarTime& time,
                                        const PlanetSolarSystemConfig& solar = {});
[[nodiscard]] PlanetCelestialDiagnostics
planet_celestial_diagnostics(const PlanetSolarTime& time,
                             const PlanetSolarSystemConfig& solar = {});
[[nodiscard]] PlanetCelestialBody planet_celestial_sun_body(const PlanetCelestialSystem& celestial);
[[nodiscard]] PlanetCelestialBody
planet_celestial_moon_body(const PlanetCelestialSystem& celestial);
[[nodiscard]] PlanetCelestialBodyRenderPlacement
planet_celestial_body_render_placement(const PlanetCelestialBody& body,
                                       const PlanetCelestialBodyRenderPlacementInputs& inputs);
[[nodiscard]] PlanetCelestialLighting
planet_celestial_lighting(const PlanetCelestialSystem& celestial);
[[nodiscard]] float
planet_celestial_sun_elevation_degrees(const PlanetCelestialSystem& celestial,
                                       cubey::math::DVec3 camera_world_position_m);
[[nodiscard]] float
planet_celestial_visible_disk_light_fraction(const PlanetCelestialSystem& celestial,
                                             cubey::math::DVec3 camera_world_position_m);
[[nodiscard]] float planet_celestial_view_light_fraction(const PlanetCelestialSystem& celestial,
                                                         cubey::math::DVec3 camera_world_position_m,
                                                         const PlanetExposureView& view);
[[nodiscard]] float planet_celestial_auto_exposure(float sun_elevation_degrees,
                                                   const PlanetExposureConfig& exposure);
[[nodiscard]] float planet_celestial_orbit_auto_exposure(float visible_disk_light_fraction,
                                                         const PlanetExposureConfig& exposure);
[[nodiscard]] float planet_celestial_display_exposure(const PlanetCelestialSystem& celestial,
                                                      cubey::math::DVec3 camera_world_position_m,
                                                      const PlanetExposureConfig& exposure);
[[nodiscard]] float planet_celestial_display_exposure(const PlanetCelestialSystem& celestial,
                                                      cubey::math::DVec3 camera_world_position_m,
                                                      const PlanetExposureConfig& exposure,
                                                      float surface_reference_weight);
[[nodiscard]] float planet_celestial_display_exposure(const PlanetCelestialSystem& celestial,
                                                      cubey::math::DVec3 camera_world_position_m,
                                                      const PlanetExposureConfig& exposure,
                                                      float surface_reference_weight,
                                                      const PlanetExposureView& view);
[[nodiscard]] PlanetAtmosphereInputs
planet_atmosphere_inputs(const PlanetCelestialSystem& celestial,
                         const PlanetCelestialLighting& lighting,
                         cubey::math::DVec3 camera_world_position_m, float planet_radius_m,
                         float atmosphere_outer_radius_m);
[[nodiscard]] PlanetSkyFrameUniforms
planet_sky_frame_uniforms(const PlanetCelestialSystem& celestial,
                          const PlanetSkyFrameUniformInputs& inputs);
[[nodiscard]] cubey::render::MaterialPassInfo planet_sky_pass_info();
[[nodiscard]] PlanetCelestialBodyFrameUniforms planet_celestial_body_frame_uniforms(
    const PlanetCelestialBody& body, const PlanetCelestialBodyRenderPlacement& placement,
    const PlanetCelestialLighting& lighting, const cubey::math::Mat4& view_projection,
    const PlanetCelestialBodyFrameInputs& inputs = {});
[[nodiscard]] cubey::render::MaterialPassInfo planet_celestial_body_pass_info();

class PlanetSkyFrame {
  public:
    PlanetSkyFrame() = default;

    PlanetSkyFrame(const PlanetSkyFrame&) = delete;
    PlanetSkyFrame& operator=(const PlanetSkyFrame&) = delete;
    PlanetSkyFrame(PlanetSkyFrame&&) = delete;
    PlanetSkyFrame& operator=(PlanetSkyFrame&&) = delete;

    void create_materials(const cubey::vulkan::Device& device,
                          const PlanetSkyFrameMaterialConfig& config);
    void create_pipeline(const cubey::vulkan::Device& device,
                         const PlanetSkyFramePipelineConfig& config);
    void destroy_pipeline();
    void destroy();

    void upload(cubey::render::FrameSlot frame_slot, const PlanetSkyFrameUniforms& uniforms) const;
    void record_pass(const cubey::vulkan::CommandRecorder& recorder,
                     cubey::render::ColorTargetView target,
                     cubey::render::FrameSlot frame_slot) const;

    [[nodiscard]] bool materials_created() const noexcept;
    [[nodiscard]] const cubey::render::FrameUniformMaterialInstance<PlanetSkyFrameUniforms>&
    material() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource& pipeline() const;

  private:
    std::optional<cubey::render::FrameUniformMaterialInstance<PlanetSkyFrameUniforms>> material_;
    std::optional<cubey::render::GraphicsPipelineResource> pipeline_;
};

class PlanetCelestialBodyFrame {
  public:
    PlanetCelestialBodyFrame() = default;

    PlanetCelestialBodyFrame(const PlanetCelestialBodyFrame&) = delete;
    PlanetCelestialBodyFrame& operator=(const PlanetCelestialBodyFrame&) = delete;
    PlanetCelestialBodyFrame(PlanetCelestialBodyFrame&&) = delete;
    PlanetCelestialBodyFrame& operator=(PlanetCelestialBodyFrame&&) = delete;

    void create_materials(const cubey::vulkan::Device& device,
                          const PlanetCelestialBodyFrameMaterialConfig& config);
    void create_pipeline(const cubey::vulkan::Device& device,
                         const PlanetCelestialBodyFramePipelineConfig& config);
    void destroy_pipeline();
    void destroy();

    void upload(cubey::render::FrameSlot frame_slot,
                const PlanetCelestialBodyFrameUniforms& uniforms) const;
    void record_pass(const cubey::vulkan::CommandRecorder& recorder,
                     const cubey::render::RenderTargetView& target,
                     cubey::render::FrameSlot frame_slot, const cubey::render::Mesh& mesh) const;

    [[nodiscard]] bool materials_created() const noexcept;
    [[nodiscard]] const cubey::render::FrameUniformMaterialInstance<
        PlanetCelestialBodyFrameUniforms>&
    material() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource& pipeline() const;

  private:
    std::optional<cubey::render::FrameUniformMaterialInstance<PlanetCelestialBodyFrameUniforms>>
        material_;
    std::optional<cubey::render::GraphicsPipelineResource> pipeline_;
};

} // namespace cubey::projects::planet
