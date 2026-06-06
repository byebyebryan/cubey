#pragma once

#include "planet_config.h"

#include <cubey/core/run_config.h>
#include <cubey/core/math.h>
#include <cubey/render/atmosphere_environment.h>
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

inline constexpr float kPlanetMeanSolarDayHours = 24.0F;
inline constexpr float kPlanetMeanTropicalYearDays = 365.2422F;
inline constexpr float kPlanetEarthSiderealRotationHours = 23.9345F;
inline constexpr float kPlanetEarthSiderealRotationPeriodDays =
    kPlanetEarthSiderealRotationHours / kPlanetMeanSolarDayHours;
inline constexpr float kPlanetMoonSiderealOrbitPeriodDays = 27.321661F;
inline constexpr float kPlanetMoonOrbitInclinationRad = 0.08979719F;
inline constexpr float kPlanetDefaultMoonOrbitPhaseOffsetCycles = 0.5980231F;
inline constexpr float kPlanetFullMoonLightIntensity = 0.055F;

enum class PlanetCelestialBodyType : std::uint8_t {
    Sun,
    Moon,
};

struct PlanetCelestialSun {
    bool visible = true;
    cubey::math::Vec3 direction{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 color{1.0F, 0.94F, 0.82F};
    float intensity = 2.25F;
    float angular_radius_rad = 0.004675F;
    float distance_m = 149597870700.0F;
    float radius_m = 696340000.0F;
};

struct PlanetCelestialMoon {
    bool visible = true;
    cubey::math::Vec3 direction{0.0F, 0.0F, 1.0F};
    cubey::math::Vec3 color{0.58F, 0.62F, 0.74F};
    float intensity = 0.0F;
    float angular_radius_rad = 0.00452F;
    float distance_m = 384400000.0F;
    float radius_m = 1737400.0F;
    float phase_fraction = 0.5F;
};

struct PlanetCelestialBody {
    PlanetCelestialBodyType type = PlanetCelestialBodyType::Moon;
    bool visible = true;
    cubey::math::Vec3 direction{0.0F, 0.0F, 1.0F};
    cubey::math::Vec3 color{1.0F, 1.0F, 1.0F};
    float intensity = 1.0F;
    float angular_radius_rad = 0.00452F;
    float distance_m = 1.0F;
    float radius_m = 1.0F;
    float phase_fraction = 0.5F;
};

struct PlanetCelestialSystem {
    PlanetCelestialSun sun{};
    PlanetCelestialMoon moon{};
    float simulation_day = 0.0F;
    float planet_rotation_angle_rad = 0.0F;
    float planet_orbit_angle_rad = 0.0F;
    float moon_orbit_angle_rad = 0.0F;
};

struct PlanetCelestialDiagnostics {
    float mean_solar_day_hours = kPlanetMeanSolarDayHours;
    float sidereal_rotation_hours = kPlanetEarthSiderealRotationHours;
    float tropical_year_days = kPlanetMeanTropicalYearDays;
    float lunar_sidereal_month_days = kPlanetMoonSiderealOrbitPeriodDays;
    float lunar_synodic_month_days = 29.53068F;
    float axial_tilt_rad = 0.0F;
    float lunar_orbit_inclination_rad = 0.0F;
    float moon_phase_fraction = 0.0F;
    cubey::math::Vec3 equator_plane_normal{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 ecliptic_plane_normal{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 moon_orbit_plane_normal{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 sun_direction{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 moon_direction{0.0F, 0.0F, 1.0F};
};

struct PlanetCelestialBodyRenderPlacementInputs {
    cubey::math::Vec3 camera_render_position_m{0.0F, 0.0F, 0.0F};
    cubey::math::DVec3 camera_world_position_m{0.0, 0.0, 0.0};
    cubey::math::DVec3 planet_center_world_position_m{0.0, 0.0, 0.0};
    float near_plane_m = 1.0F;
    float far_plane_m = 1000.0F;
    float angular_radius_scale = 1.0F;
    float shell_distance_fraction = 0.58F;
};

struct PlanetCelestialBodyRenderPlacement {
    bool visible = false;
    cubey::math::Vec3 center_render_m{0.0F, 0.0F, 0.0F};
    float radius_render_m = 0.0F;
    float shell_distance_m = 0.0F;
    float angular_radius_rad = 0.0F;
};

struct PlanetSolarTime {
    float day_of_year = 80.0F;
    float time_hours = 5.5F;
    float hours_per_second = 0.5F;
};

[[nodiscard]] PlanetSolarTime planet_solar_time_from_run_config(const RunConfig& config);

struct PlanetSolarSystemConfig {
    float axial_tilt_rad = 0.4090928F;
    float planet_rotation_period_days = kPlanetEarthSiderealRotationPeriodDays;
    float planet_orbit_period_days = kPlanetMeanTropicalYearDays;
    float moon_orbit_period_days = kPlanetMoonSiderealOrbitPeriodDays;
    float moon_orbit_inclination_rad = kPlanetMoonOrbitInclinationRad;
    float moon_orbit_phase_offset_cycles = kPlanetDefaultMoonOrbitPhaseOffsetCycles;
    float equinox_day = 80.0F;
    float sun_distance_m = 149597870700.0F;
    float sun_radius_m = 696340000.0F;
    float moon_distance_m = 384400000.0F;
    float moon_radius_m = 1737400.0F;
};

struct PlanetSkyFrameUniforms {
    cubey::math::Vec4 camera_right_aspect;
    cubey::math::Vec4 camera_up_tan_half_fovy;
    cubey::math::Vec4 camera_forward_enabled;
    cubey::math::Vec4 sun_direction_radius;
    cubey::math::Vec4 sun_color_intensity;
    cubey::math::Vec4 sun_disk_glow;
    cubey::math::Vec4 camera_position_radius;
    cubey::math::Vec4 background_space_limb;
    cubey::math::Vec4 atmosphere_mode_options;
};

static_assert(sizeof(PlanetSkyFrameUniforms) == sizeof(float) * 36U);

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
};

struct PlanetCelestialBodyAtmosphereInputs {
    cubey::math::Vec3 camera_position_m{0.0F, 0.0F, 0.0F};
    float planet_radius_m = 0.0F;
    float atmosphere_outer_radius_m = 0.0F;
};

struct PlanetCelestialLighting {
    cubey::math::Vec3 primary_light_direction{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 primary_light_color{1.0F, 0.94F, 0.82F};
    float primary_light_intensity = 0.9F;
    float primary_light_angular_radius_rad = 0.004675F;
    cubey::math::Vec3 moon_light_direction{0.0F, 0.0F, 1.0F};
    cubey::math::Vec3 moon_light_color{0.56F, 0.64F, 0.86F};
    float moon_light_intensity = 0.0F;
    cubey::math::Vec3 ambient_color{0.040F, 0.050F, 0.070F};
    float ambient_intensity = 0.12F;
    cubey::math::Vec3 haze_color{0.085F, 0.125F, 0.185F};
};

struct PlanetCelestialBodyFrameInputs {
    cubey::math::Vec3 camera_render_position_m{0.0F, 0.0F, 0.0F};
    PlanetCelestialBodyAtmosphereInputs atmosphere{};
};

struct PlanetAtmosphereInputs {
    cubey::math::Vec3 camera_position_m{0.0F, 0.0F, 0.0F};
    float camera_radius_m = 0.0F;
    float camera_altitude_m = 0.0F;
    float planet_radius_m = kPlanetDefaultRadiusM;
    float atmosphere_outer_radius_m = kPlanetDefaultRadiusM + kPlanetDefaultAtmosphereHeightM;
    cubey::math::Vec3 sun_direction{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 sun_color{1.0F, 0.94F, 0.82F};
    float sun_intensity = 1.0F;
    float sun_angular_radius_rad = 0.004675F;
    cubey::math::Vec3 moon_direction{0.0F, 0.0F, 1.0F};
    float moon_phase_fraction = 0.5F;
    float moon_angular_radius_rad = 0.00452F;
};

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
[[nodiscard]] PlanetAtmosphereInputs
planet_atmosphere_inputs(const PlanetCelestialSystem& celestial,
                         const PlanetCelestialLighting& lighting,
                         cubey::math::DVec3 camera_world_position_m, float planet_radius_m,
                         float atmosphere_outer_radius_m);
[[nodiscard]] cubey::render::AtmosphereEnvironmentConfig
planet_atmosphere_environment_config(const PlanetAtmosphereInputs& inputs);
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
