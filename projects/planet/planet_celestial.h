#pragma once

#include "planet_config.h"

#include <cubey/core/math.h>
#include <cubey/core/run_config.h>
#include <cubey/render/celestial_body_frame.h>
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

using PlanetCelestialBodyFrameUniforms = cubey::render::CelestialBodyFrameUniforms;

using PlanetCelestialBodyAtmosphereInputs = cubey::render::CelestialBodyAtmosphereInputs;

using PlanetCelestialLighting = cubey::render::CelestialLighting;
using PlanetCelestialBodyFrameInputs = cubey::render::CelestialBodyFrameInputs;

using PlanetAtmosphereInputs = cubey::render::CelestialAtmosphereInputs;
using PlanetCelestialBodyFrameMaterialConfig = cubey::render::CelestialBodyFrameMaterialConfig;
using PlanetCelestialBodyFramePipelineConfig = cubey::render::CelestialBodyFramePipelineConfig;

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
[[nodiscard]] PlanetCelestialBodyFrameUniforms planet_celestial_body_frame_uniforms(
    const PlanetCelestialBody& body, const PlanetCelestialBodyRenderPlacement& placement,
    const PlanetCelestialLighting& lighting, const cubey::math::Mat4& view_projection,
    const PlanetCelestialBodyFrameInputs& inputs = {});
[[nodiscard]] cubey::render::MaterialPassInfo planet_celestial_body_pass_info();
using PlanetCelestialBodyFrame = cubey::render::CelestialBodyFrame;

} // namespace cubey::projects::planet
