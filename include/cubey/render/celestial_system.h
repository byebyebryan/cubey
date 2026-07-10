#pragma once

#include <cubey/core/math.h>
#include <cubey/render/view_ray_basis_3d.h>

#include <cstdint>

namespace cubey::render {

inline constexpr float kCelestialMeanSolarDayHours = 24.0F;
inline constexpr float kCelestialMeanTropicalYearDays = 365.2422F;
inline constexpr float kCelestialEarthSiderealRotationHours = 23.9345F;
inline constexpr float kCelestialEarthSiderealRotationPeriodDays =
    kCelestialEarthSiderealRotationHours / kCelestialMeanSolarDayHours;
inline constexpr float kCelestialMoonSiderealOrbitPeriodDays = 27.321661F;
inline constexpr float kCelestialMoonOrbitInclinationRad = 0.08979719F;
inline constexpr float kCelestialDefaultMoonOrbitPhaseOffsetCycles = 0.5980231F;
inline constexpr float kCelestialFullMoonLightIntensity = 0.055F;
inline constexpr float kCelestialDefaultDaylightExposure = -2.35F;
inline constexpr float kCelestialDefaultTwilightExposure = -1.55F;
inline constexpr float kCelestialDefaultNightExposure = 2.80F;
inline constexpr float kCelestialDefaultPlanetRadiusM = 6371000.0F;
inline constexpr float kCelestialDefaultAtmosphereHeightM = 100000.0F;
inline const math::Vec3 kCelestialMoonSurfaceColor{0.58F, 0.62F, 0.74F};
inline const math::Vec3 kCelestialMoonLightColor{0.56F, 0.64F, 0.86F};

struct CelestialExposureConfig {
    bool auto_exposure_enabled = true;
    float manual_exposure = 0.0F;
    float daylight_exposure = kCelestialDefaultDaylightExposure;
    float twilight_exposure = kCelestialDefaultTwilightExposure;
    float night_exposure = kCelestialDefaultNightExposure;
};

struct CelestialExposureView {
    ViewRayBasis3D view_rays{};
    float planet_radius_m = kCelestialDefaultPlanetRadiusM;
};

enum class CelestialBodyType : std::uint8_t {
    Sun,
    Moon,
};

struct CelestialSun {
    bool visible = true;
    cubey::math::Vec3 direction{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 color{1.0F, 0.94F, 0.82F};
    float intensity = 2.25F;
    float angular_radius_rad = 0.004675F;
    float distance_m = 149597870700.0F;
    float radius_m = 696340000.0F;
};

struct CelestialMoon {
    bool visible = true;
    cubey::math::Vec3 direction{0.0F, 0.0F, 1.0F};
    cubey::math::Vec3 color{kCelestialMoonSurfaceColor};
    float intensity = 0.0F;
    float angular_radius_rad = 0.00452F;
    float distance_m = 384400000.0F;
    float radius_m = 1737400.0F;
    float phase_fraction = 0.5F;
};

struct CelestialBody {
    CelestialBodyType type = CelestialBodyType::Moon;
    bool visible = true;
    cubey::math::Vec3 direction{0.0F, 0.0F, 1.0F};
    cubey::math::Vec3 color{1.0F, 1.0F, 1.0F};
    float intensity = 1.0F;
    float angular_radius_rad = 0.00452F;
    float distance_m = 1.0F;
    float radius_m = 1.0F;
    float phase_fraction = 0.5F;
};

struct CelestialSystem {
    CelestialSun sun{};
    CelestialMoon moon{};
    float simulation_day = 0.0F;
    float planet_rotation_angle_rad = 0.0F;
    float planet_orbit_angle_rad = 0.0F;
    float moon_orbit_angle_rad = 0.0F;
};

struct CelestialDiagnostics {
    float mean_solar_day_hours = kCelestialMeanSolarDayHours;
    float sidereal_rotation_hours = kCelestialEarthSiderealRotationHours;
    float tropical_year_days = kCelestialMeanTropicalYearDays;
    float lunar_sidereal_month_days = kCelestialMoonSiderealOrbitPeriodDays;
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

struct CelestialBodyRenderPlacementInputs {
    cubey::math::Vec3 camera_render_position_m{0.0F, 0.0F, 0.0F};
    cubey::math::DVec3 camera_world_position_m{0.0, 0.0, 0.0};
    cubey::math::DVec3 planet_center_world_position_m{0.0, 0.0, 0.0};
    float near_plane_m = 1.0F;
    float far_plane_m = 1000.0F;
    float angular_radius_scale = 1.0F;
    float shell_distance_fraction = 0.58F;
};

struct CelestialBodyRenderPlacement {
    bool visible = false;
    cubey::math::Vec3 center_render_m{0.0F, 0.0F, 0.0F};
    float radius_render_m = 0.0F;
    float shell_distance_m = 0.0F;
    float angular_radius_rad = 0.0F;
};

struct CelestialSolarTime {
    float day_of_year = 80.0F;
    float time_hours = 5.5F;
    float hours_per_second = 0.5F;
};

struct CelestialSolarSystemConfig {
    float axial_tilt_rad = 0.4090928F;
    float planet_rotation_period_days = kCelestialEarthSiderealRotationPeriodDays;
    float planet_orbit_period_days = kCelestialMeanTropicalYearDays;
    float moon_orbit_period_days = kCelestialMoonSiderealOrbitPeriodDays;
    float moon_orbit_inclination_rad = kCelestialMoonOrbitInclinationRad;
    float moon_orbit_phase_offset_cycles = kCelestialDefaultMoonOrbitPhaseOffsetCycles;
    float equinox_day = 80.0F;
    float sun_distance_m = 149597870700.0F;
    float sun_radius_m = 696340000.0F;
    float moon_distance_m = 384400000.0F;
    float moon_radius_m = 1737400.0F;
};

struct CelestialLighting {
    cubey::math::Vec3 primary_light_direction{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 primary_light_color{1.0F, 0.94F, 0.82F};
    float primary_light_intensity = 0.9F;
    float primary_light_angular_radius_rad = 0.004675F;
    cubey::math::Vec3 moon_light_direction{0.0F, 0.0F, 1.0F};
    cubey::math::Vec3 moon_light_color{kCelestialMoonLightColor};
    float moon_light_intensity = 0.0F;
    cubey::math::Vec3 ambient_color{0.040F, 0.050F, 0.070F};
    float ambient_intensity = 0.12F;
    cubey::math::Vec3 haze_color{0.085F, 0.125F, 0.185F};
};

struct CelestialAtmosphereInputs {
    cubey::math::Vec3 camera_position_m{0.0F, 0.0F, 0.0F};
    float camera_radius_m = 0.0F;
    float camera_altitude_m = 0.0F;
    float planet_radius_m = kCelestialDefaultPlanetRadiusM;
    float atmosphere_outer_radius_m = kCelestialDefaultPlanetRadiusM + kCelestialDefaultAtmosphereHeightM;
    cubey::math::Vec3 sun_direction{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 sun_color{1.0F, 0.94F, 0.82F};
    float sun_intensity = 1.0F;
    float sun_angular_radius_rad = 0.004675F;
    cubey::math::Vec3 moon_direction{0.0F, 0.0F, 1.0F};
    float moon_phase_fraction = 0.5F;
    float moon_angular_radius_rad = 0.00452F;
};

[[nodiscard]] float celestial_solar_time_simulation_day(const CelestialSolarTime& time);
void celestial_solar_time_advance(CelestialSolarTime& time, double delta_seconds);
[[nodiscard]] float celestial_synodic_month_days(const CelestialSolarSystemConfig& solar = {});
[[nodiscard]] CelestialSystem
celestial_system_from_solar_time(const CelestialSolarTime& time,
                                 const CelestialSolarSystemConfig& solar = {});
[[nodiscard]] CelestialDiagnostics
celestial_diagnostics(const CelestialSolarTime& time,
                      const CelestialSolarSystemConfig& solar = {});
[[nodiscard]] CelestialBody celestial_sun_body(const CelestialSystem& celestial);
[[nodiscard]] CelestialBody celestial_moon_body(const CelestialSystem& celestial);
[[nodiscard]] CelestialBodyRenderPlacement
celestial_body_render_placement(const CelestialBody& body,
                                const CelestialBodyRenderPlacementInputs& inputs);
[[nodiscard]] CelestialLighting celestial_lighting(const CelestialSystem& celestial);
[[nodiscard]] float celestial_sun_elevation_degrees(const CelestialSystem& celestial,
                                                    cubey::math::DVec3 camera_world_position_m);
[[nodiscard]] float
celestial_visible_disk_light_fraction(const CelestialSystem& celestial,
                                      cubey::math::DVec3 camera_world_position_m);
[[nodiscard]] float celestial_view_light_fraction(const CelestialSystem& celestial,
                                                  cubey::math::DVec3 camera_world_position_m,
                                                  const CelestialExposureView& view);
[[nodiscard]] float celestial_auto_exposure(float sun_elevation_degrees,
                                            const CelestialExposureConfig& exposure);
[[nodiscard]] float celestial_orbit_auto_exposure(float visible_disk_light_fraction,
                                                  const CelestialExposureConfig& exposure);
[[nodiscard]] float celestial_display_exposure(const CelestialSystem& celestial,
                                               cubey::math::DVec3 camera_world_position_m,
                                               const CelestialExposureConfig& exposure);
[[nodiscard]] float celestial_display_exposure(const CelestialSystem& celestial,
                                               cubey::math::DVec3 camera_world_position_m,
                                               const CelestialExposureConfig& exposure,
                                               float surface_reference_weight);
[[nodiscard]] float celestial_display_exposure(const CelestialSystem& celestial,
                                               cubey::math::DVec3 camera_world_position_m,
                                               const CelestialExposureConfig& exposure,
                                               float surface_reference_weight,
                                               const CelestialExposureView& view);
[[nodiscard]] CelestialAtmosphereInputs
celestial_atmosphere_inputs(const CelestialSystem& celestial, const CelestialLighting& lighting,
                            cubey::math::DVec3 camera_world_position_m, float planet_radius_m,
                            float atmosphere_outer_radius_m);

} // namespace cubey::render
