#include <cubey/render/celestial_system.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace cubey::render {
namespace {

using cubey::math::DVec3;
using cubey::math::Vec3;

[[nodiscard]] Vec3 normalized_or_up(Vec3 direction) {
    if (glm::dot(direction, direction) <= 0.0F) {
        return {0.0F, 1.0F, 0.0F};
    }
    return glm::normalize(direction);
}

[[nodiscard]] bool finite_positive(float value) {
    return std::isfinite(value) && value > 0.0F;
}

[[nodiscard]] float wrap_unit(float value) {
    float wrapped = std::fmod(value, 1.0F);
    if (wrapped < 0.0F) {
        wrapped += 1.0F;
    }
    return wrapped;
}

[[nodiscard]] float moon_illumination_fraction(float phase_fraction) {
    constexpr float kTwoPi = std::numbers::pi_v<float> * 2.0F;
    return std::clamp(0.5F - 0.5F * std::cos(wrap_unit(phase_fraction) * kTwoPi), 0.0F, 1.0F);
}

[[nodiscard]] Vec3 rotate_y(Vec3 value, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return {
        (value.x * c) + (value.z * s),
        value.y,
        (-value.x * s) + (value.z * c),
    };
}

[[nodiscard]] Vec3 rotate_x(Vec3 value, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return {
        value.x,
        (value.y * c) - (value.z * s),
        (value.y * s) + (value.z * c),
    };
}

[[nodiscard]] float angular_radius(float radius_m, float distance_m) {
    return std::asin(std::clamp(radius_m / std::max(distance_m, 1.0F), -1.0F, 1.0F));
}

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
    if (edge0 == edge1) {
        return value < edge0 ? 0.0F : 1.0F;
    }
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - (2.0F * t));
}

[[nodiscard]] DVec3 to_double(Vec3 value) {
    return {static_cast<double>(value.x), static_cast<double>(value.y),
            static_cast<double>(value.z)};
}

[[nodiscard]] Vec3 to_float(DVec3 value) {
    return {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z)};
}

[[nodiscard]] bool ray_sphere_surface_normal(DVec3 origin, Vec3 direction, double radius_m,
                                             Vec3& normal) {
    if (radius_m <= 0.0 || glm::length(direction) <= 0.000001F) {
        return false;
    }
    const DVec3 ray = glm::normalize(to_double(direction));
    const double b = glm::dot(origin, ray);
    const double c = glm::dot(origin, origin) - (radius_m * radius_m);
    const double discriminant = (b * b) - c;
    if (discriminant < 0.0) {
        return false;
    }

    const double root = std::sqrt(discriminant);
    double t = -b - root;
    if (t < 0.0) {
        t = -b + root;
    }
    if (t < 0.0) {
        return false;
    }

    const DVec3 hit = origin + ray * t;
    if (glm::length(hit) <= 0.000001) {
        return false;
    }
    normal = normalized_or_up(to_float(glm::normalize(hit)));
    return true;
}

[[nodiscard]] float sky_direction_light_fraction(const CelestialSystem& celestial,
                                                 const ViewRayBasis3D& view_rays) {
    const Vec3 forward = normalized_or_up(Vec3{view_rays.forward});
    const float sun_alignment = glm::dot(forward, normalized_or_up(celestial.sun.direction));
    return std::clamp((sun_alignment * 0.5F) + 0.5F, 0.0F, 1.0F);
}

} // namespace

float celestial_solar_time_simulation_day(const CelestialSolarTime& time) {
    return std::max(time.day_of_year - 1.0F, 0.0F) +
           std::clamp(time.time_hours, 0.0F, 24.0F) / 24.0F;
}

void celestial_solar_time_advance(CelestialSolarTime& time, double delta_seconds) {
    if (time.hours_per_second == 0.0F || delta_seconds <= 0.0) {
        return;
    }
    const double advanced_hours = static_cast<double>(time.time_hours) +
                                  delta_seconds * static_cast<double>(time.hours_per_second);
    const double day_offset = std::floor(advanced_hours / 24.0);
    double wrapped_hours = std::fmod(advanced_hours, 24.0);
    if (wrapped_hours < 0.0) {
        wrapped_hours += 24.0;
    }
    time.time_hours = static_cast<float>(wrapped_hours);
    time.day_of_year += static_cast<float>(day_offset);
    while (time.day_of_year > kCelestialMeanTropicalYearDays) {
        time.day_of_year -= kCelestialMeanTropicalYearDays;
    }
    while (time.day_of_year < 1.0F) {
        time.day_of_year += kCelestialMeanTropicalYearDays;
    }
}

float celestial_synodic_month_days(const CelestialSolarSystemConfig& solar) {
    const float lunar_rate = 1.0F / std::max(solar.moon_orbit_period_days, 0.0001F);
    const float earth_orbit_rate = 1.0F / std::max(solar.planet_orbit_period_days, 0.0001F);
    const float synodic_rate = std::max(lunar_rate - earth_orbit_rate, 0.0001F);
    return 1.0F / synodic_rate;
}

CelestialSystem celestial_system_from_solar_time(const CelestialSolarTime& time,
                                                 const CelestialSolarSystemConfig& solar) {
    constexpr float kTwoPi = std::numbers::pi_v<float> * 2.0F;
    const float simulation_day = celestial_solar_time_simulation_day(time);
    const float rotation_angle =
        kTwoPi * simulation_day / std::max(solar.planet_rotation_period_days, 0.0001F);
    const float orbit_angle = kTwoPi * (simulation_day - solar.equinox_day) /
                              std::max(solar.planet_orbit_period_days, 0.0001F);
    const float moon_orbit_angle =
        kTwoPi * (simulation_day / std::max(solar.moon_orbit_period_days, 0.0001F) +
                  solar.moon_orbit_phase_offset_cycles);

    const Vec3 sun_in_orbital_frame =
        normalized_or_up({std::cos(orbit_angle), 0.0F, -std::sin(orbit_angle)});
    const Vec3 sun_in_tilted_frame =
        normalized_or_up(rotate_x(sun_in_orbital_frame, solar.axial_tilt_rad));
    const Vec3 sun_in_planet_frame = normalized_or_up(rotate_y(sun_in_tilted_frame, -rotation_angle));

    const float moon_inclination = std::clamp(solar.moon_orbit_inclination_rad, -1.4F, 1.4F);
    const Vec3 moon_orbit_frame = normalized_or_up({
        std::cos(moon_orbit_angle),
        std::sin(moon_orbit_angle) * std::sin(moon_inclination),
        -std::sin(moon_orbit_angle) * std::cos(moon_inclination),
    });
    const Vec3 moon_in_tilted_frame =
        normalized_or_up(rotate_x(moon_orbit_frame, solar.axial_tilt_rad));
    const Vec3 moon_in_planet_frame =
        normalized_or_up(rotate_y(moon_in_tilted_frame, -rotation_angle));
    const float phase_fraction = wrap_unit((moon_orbit_angle - orbit_angle) / kTwoPi);

    return {
        .sun =
            {
                .visible = true,
                .direction = sun_in_planet_frame,
                .color = {1.0F, 0.94F, 0.82F},
                .intensity = 2.25F,
                .angular_radius_rad = angular_radius(solar.sun_radius_m, solar.sun_distance_m),
                .distance_m = solar.sun_distance_m,
                .radius_m = solar.sun_radius_m,
            },
        .moon =
            {
                .visible = true,
                .direction = moon_in_planet_frame,
                .color = {0.58F, 0.62F, 0.74F},
                .intensity = 0.0F,
                .angular_radius_rad = angular_radius(solar.moon_radius_m, solar.moon_distance_m),
                .distance_m = solar.moon_distance_m,
                .radius_m = solar.moon_radius_m,
                .phase_fraction = phase_fraction,
            },
        .simulation_day = simulation_day,
        .planet_rotation_angle_rad = rotation_angle,
        .planet_orbit_angle_rad = orbit_angle,
        .moon_orbit_angle_rad = moon_orbit_angle,
    };
}

CelestialDiagnostics celestial_diagnostics(const CelestialSolarTime& time,
                                           const CelestialSolarSystemConfig& solar) {
    constexpr float kTwoPi = std::numbers::pi_v<float> * 2.0F;
    const float simulation_day = celestial_solar_time_simulation_day(time);
    const float rotation_angle =
        kTwoPi * simulation_day / std::max(solar.planet_rotation_period_days, 0.0001F);
    const float moon_inclination = std::clamp(solar.moon_orbit_inclination_rad, -1.4F, 1.4F);
    const Vec3 ecliptic_normal_orbital_frame{0.0F, 1.0F, 0.0F};
    const Vec3 moon_plane_normal_orbital_frame =
        normalized_or_up({0.0F, std::cos(moon_inclination), std::sin(moon_inclination)});
    const Vec3 ecliptic_normal_tilted =
        normalized_or_up(rotate_x(ecliptic_normal_orbital_frame, solar.axial_tilt_rad));
    const Vec3 moon_plane_normal_tilted =
        normalized_or_up(rotate_x(moon_plane_normal_orbital_frame, solar.axial_tilt_rad));
    const CelestialSystem celestial = celestial_system_from_solar_time(time, solar);

    return {
        .mean_solar_day_hours = kCelestialMeanSolarDayHours,
        .sidereal_rotation_hours = solar.planet_rotation_period_days * kCelestialMeanSolarDayHours,
        .tropical_year_days = solar.planet_orbit_period_days,
        .lunar_sidereal_month_days = solar.moon_orbit_period_days,
        .lunar_synodic_month_days = celestial_synodic_month_days(solar),
        .axial_tilt_rad = solar.axial_tilt_rad,
        .lunar_orbit_inclination_rad = moon_inclination,
        .moon_phase_fraction = celestial.moon.phase_fraction,
        .equator_plane_normal = {0.0F, 1.0F, 0.0F},
        .ecliptic_plane_normal = normalized_or_up(rotate_y(ecliptic_normal_tilted, -rotation_angle)),
        .moon_orbit_plane_normal =
            normalized_or_up(rotate_y(moon_plane_normal_tilted, -rotation_angle)),
        .sun_direction = celestial.sun.direction,
        .moon_direction = celestial.moon.direction,
    };
}

CelestialBody celestial_sun_body(const CelestialSystem& celestial) {
    return {
        .type = CelestialBodyType::Sun,
        .visible = celestial.sun.visible,
        .direction = normalized_or_up(celestial.sun.direction),
        .color = celestial.sun.color,
        .intensity = celestial.sun.intensity,
        .angular_radius_rad = celestial.sun.angular_radius_rad,
        .distance_m = celestial.sun.distance_m,
        .radius_m = celestial.sun.radius_m,
        .phase_fraction = 1.0F,
    };
}

CelestialBody celestial_moon_body(const CelestialSystem& celestial) {
    return {
        .type = CelestialBodyType::Moon,
        .visible = celestial.moon.visible,
        .direction = normalized_or_up(celestial.moon.direction),
        .color = celestial.moon.color,
        .intensity = celestial.moon.intensity,
        .angular_radius_rad = celestial.moon.angular_radius_rad,
        .distance_m = celestial.moon.distance_m,
        .radius_m = celestial.moon.radius_m,
        .phase_fraction = celestial.moon.phase_fraction,
    };
}

CelestialBodyRenderPlacement
celestial_body_render_placement(const CelestialBody& body,
                                const CelestialBodyRenderPlacementInputs& inputs) {
    const float near_plane = std::max(inputs.near_plane_m, 0.001F);
    const float far_plane = std::max(inputs.far_plane_m, near_plane + 1.0F);
    const float shell_distance =
        std::clamp(far_plane * std::clamp(inputs.shell_distance_fraction, 0.10F, 0.90F),
                   near_plane * 4.0F, far_plane * 0.90F);
    const float scaled_angular_radius = std::clamp(
        body.angular_radius_rad * std::max(inputs.angular_radius_scale, 0.0F), 0.00001F, 0.35F);
    const float render_radius = std::tan(scaled_angular_radius) * shell_distance;
    const bool visible =
        body.visible && finite_positive(body.angular_radius_rad) && finite_positive(render_radius);
    const Vec3 body_direction = normalized_or_up(body.direction);
    const DVec3 body_center_world =
        inputs.planet_center_world_position_m +
        DVec3{static_cast<double>(body_direction.x), static_cast<double>(body_direction.y),
              static_cast<double>(body_direction.z)} *
            static_cast<double>(std::max(body.distance_m, 1.0F));
    const DVec3 camera_to_body = body_center_world - inputs.camera_world_position_m;
    Vec3 render_direction = body_direction;
    if (glm::length(camera_to_body) > 0.000001) {
        render_direction = normalized_or_up({static_cast<float>(camera_to_body.x),
                                             static_cast<float>(camera_to_body.y),
                                             static_cast<float>(camera_to_body.z)});
    }

    return {
        .visible = visible,
        .center_render_m = inputs.camera_render_position_m + render_direction * shell_distance,
        .radius_render_m = visible ? render_radius : 0.0F,
        .shell_distance_m = shell_distance,
        .angular_radius_rad = scaled_angular_radius,
    };
}

CelestialLighting celestial_lighting(const CelestialSystem& celestial) {
    const float moon_illumination = moon_illumination_fraction(celestial.moon.phase_fraction);
    return {
        .primary_light_direction = normalized_or_up(celestial.sun.direction),
        .primary_light_color = celestial.sun.color,
        .primary_light_intensity = 0.88F,
        .primary_light_angular_radius_rad = celestial.sun.angular_radius_rad,
        .moon_light_direction = normalized_or_up(celestial.moon.direction),
        .moon_light_color = {0.56F, 0.64F, 0.86F},
        .moon_light_intensity = kCelestialFullMoonLightIntensity * moon_illumination,
        .ambient_color = {0.040F, 0.050F, 0.070F},
        .ambient_intensity = 0.12F,
        .haze_color = {0.085F, 0.125F, 0.185F},
    };
}

float celestial_sun_elevation_degrees(const CelestialSystem& celestial,
                                      DVec3 camera_world_position_m) {
    constexpr float kRadiansToDegrees = 180.0F / std::numbers::pi_v<float>;
    if (glm::length(camera_world_position_m) <= 0.000001) {
        return 90.0F;
    }
    const DVec3 camera_up_d = glm::normalize(camera_world_position_m);
    const Vec3 camera_up{
        static_cast<float>(camera_up_d.x),
        static_cast<float>(camera_up_d.y),
        static_cast<float>(camera_up_d.z),
    };
    const float sun_elevation = std::asin(
        std::clamp(glm::dot(normalized_or_up(celestial.sun.direction), camera_up), -1.0F, 1.0F));
    return sun_elevation * kRadiansToDegrees;
}

float celestial_visible_disk_light_fraction(const CelestialSystem& celestial,
                                            DVec3 camera_world_position_m) {
    if (glm::length(camera_world_position_m) <= 0.000001) {
        return 1.0F;
    }
    const DVec3 camera_up_d = glm::normalize(camera_world_position_m);
    const Vec3 camera_up{
        static_cast<float>(camera_up_d.x),
        static_cast<float>(camera_up_d.y),
        static_cast<float>(camera_up_d.z),
    };
    const float phase_alignment =
        glm::dot(normalized_or_up(celestial.sun.direction), normalized_or_up(camera_up));
    return std::clamp((phase_alignment * 0.5F) + 0.5F, 0.0F, 1.0F);
}

float celestial_view_light_fraction(const CelestialSystem& celestial,
                                    DVec3 camera_world_position_m,
                                    const CelestialExposureView& view) {
    if (glm::length(camera_world_position_m) <= 0.000001 || !finite_positive(view.planet_radius_m)) {
        return celestial_visible_disk_light_fraction(celestial, camera_world_position_m);
    }

    constexpr std::array<float, 5> kSamples{-0.80F, -0.40F, 0.0F, 0.40F, 0.80F};
    const Vec3 sun_direction = normalized_or_up(celestial.sun.direction);
    float accumulated_lit = 0.0F;
    std::uint32_t sample_count = 0;
    std::uint32_t hit_count = 0;
    for (float y : kSamples) {
        for (float x : kSamples) {
            ++sample_count;
            Vec3 normal{};
            if (!ray_sphere_surface_normal(camera_world_position_m,
                                           view_ray_direction(view.view_rays, {x, y}),
                                           static_cast<double>(view.planet_radius_m), normal)) {
                continue;
            }
            ++hit_count;
            accumulated_lit += std::max(glm::dot(normal, sun_direction), 0.0F);
        }
    }

    if (hit_count == 0U || sample_count == 0U) {
        return sky_direction_light_fraction(celestial, view.view_rays);
    }

    constexpr float kFullDiskMeanLuma = 0.65F;
    const float view_lit = accumulated_lit / static_cast<float>(sample_count);
    return std::clamp(view_lit / kFullDiskMeanLuma, 0.0F, 1.0F);
}

float celestial_auto_exposure(float sun_elevation_degrees, const CelestialExposureConfig& exposure) {
    const float night_to_twilight = smoothstep(-18.0F, -6.0F, sun_elevation_degrees);
    const float twilight_to_day = smoothstep(-4.0F, 18.0F, sun_elevation_degrees);
    const float night_twilight_exposure =
        std::lerp(exposure.night_exposure, exposure.twilight_exposure, night_to_twilight);
    return std::clamp(
        std::lerp(night_twilight_exposure, exposure.daylight_exposure, twilight_to_day), -4.0F,
        4.0F);
}

float celestial_orbit_auto_exposure(float visible_disk_light_fraction,
                                    const CelestialExposureConfig& exposure) {
    const float light_fraction = std::clamp(visible_disk_light_fraction, 0.0F, 1.0F);
    const float phase_blend = smoothstep(0.08F, 0.92F, light_fraction);
    const float orbit_day_exposure =
        std::lerp(exposure.daylight_exposure, exposure.twilight_exposure, 0.15F);
    const float orbit_night_exposure =
        std::lerp(exposure.twilight_exposure, exposure.night_exposure, 0.40F);
    return std::clamp(std::lerp(orbit_night_exposure, orbit_day_exposure, phase_blend), -4.0F,
                      4.0F);
}

float celestial_display_exposure(const CelestialSystem& celestial,
                                 DVec3 camera_world_position_m,
                                 const CelestialExposureConfig& exposure) {
    if (!exposure.auto_exposure_enabled) {
        return exposure.manual_exposure;
    }

    return celestial_auto_exposure(
        celestial_sun_elevation_degrees(celestial, camera_world_position_m), exposure);
}

float celestial_display_exposure(const CelestialSystem& celestial,
                                 DVec3 camera_world_position_m,
                                 const CelestialExposureConfig& exposure,
                                 float surface_reference_weight) {
    if (!exposure.auto_exposure_enabled) {
        return exposure.manual_exposure;
    }

    const float surface_weight = std::clamp(surface_reference_weight, 0.0F, 1.0F);
    const float orbit_exposure = celestial_orbit_auto_exposure(
        celestial_visible_disk_light_fraction(celestial, camera_world_position_m), exposure);
    const float surface_exposure = celestial_auto_exposure(
        celestial_sun_elevation_degrees(celestial, camera_world_position_m), exposure);
    return std::lerp(orbit_exposure, surface_exposure, surface_weight);
}

float celestial_display_exposure(const CelestialSystem& celestial,
                                 DVec3 camera_world_position_m,
                                 const CelestialExposureConfig& exposure,
                                 float surface_reference_weight,
                                 const CelestialExposureView& view) {
    if (!exposure.auto_exposure_enabled) {
        return exposure.manual_exposure;
    }

    const float surface_weight = std::clamp(surface_reference_weight, 0.0F, 1.0F);
    const float orbit_exposure = celestial_orbit_auto_exposure(
        celestial_view_light_fraction(celestial, camera_world_position_m, view), exposure);
    const float surface_exposure = celestial_auto_exposure(
        celestial_sun_elevation_degrees(celestial, camera_world_position_m), exposure);
    return std::lerp(orbit_exposure, surface_exposure, surface_weight);
}

CelestialAtmosphereInputs celestial_atmosphere_inputs(const CelestialSystem& celestial,
                                                      const CelestialLighting& lighting,
                                                      DVec3 camera_world_position_m,
                                                      float planet_radius_m,
                                                      float atmosphere_outer_radius_m) {
    const float camera_radius = static_cast<float>(glm::length(camera_world_position_m));
    return {
        .camera_position_m =
            {
                static_cast<float>(camera_world_position_m.x),
                static_cast<float>(camera_world_position_m.y),
                static_cast<float>(camera_world_position_m.z),
            },
        .camera_radius_m = camera_radius,
        .camera_altitude_m = std::max(camera_radius - planet_radius_m, 0.0F),
        .planet_radius_m = planet_radius_m,
        .atmosphere_outer_radius_m = std::max(atmosphere_outer_radius_m, planet_radius_m),
        .sun_direction = normalized_or_up(celestial.sun.direction),
        .sun_color = lighting.primary_light_color,
        .sun_intensity = lighting.primary_light_intensity,
        .sun_angular_radius_rad = celestial.sun.angular_radius_rad,
        .moon_direction = normalized_or_up(celestial.moon.direction),
        .moon_phase_fraction = celestial.moon.phase_fraction,
        .moon_angular_radius_rad = celestial.moon.angular_radius_rad,
    };
}

} // namespace cubey::render
