#include "planet_camera.h"

#include "planet_surface_field.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cubey::projects::planet {
namespace {

constexpr float kSurfaceLookDownSlope = 0.22F;
constexpr float kDragRadiansPerPixel = 0.01F;
constexpr float kSurfaceLookRadiansPerPixel = 0.006F;
constexpr float kSurfaceMinForwardUpDot = -0.94F;
constexpr float kSurfaceMaxForwardUpDot = 0.82F;
constexpr float kOrbitPoleDeadbandRadians = 0.10F;
constexpr float kZoomBase = 0.86F;
constexpr float kSurfaceCameraMinAltitudeM = 120.0F;
constexpr float kSurfaceCameraMinAltitudeRadiusRatio = 0.000012F;
constexpr float kSurfaceCameraTerrainReferenceRatio = 0.006F;
constexpr float kSurfaceMovePlanetScaleSpeedRatio = 0.00008F;

[[nodiscard]] cubey::math::Vec3 normalize_or(cubey::math::Vec3 value, cubey::math::Vec3 fallback) {
    const float length = glm::length(value);
    if (length <= 0.000001F) {
        return fallback;
    }
    return value / length;
}

[[nodiscard]] cubey::math::DVec3 normalize_or(cubey::math::DVec3 value,
                                              cubey::math::DVec3 fallback) {
    const double length = glm::length(value);
    if (length <= 0.000001) {
        return fallback;
    }
    return value / length;
}

[[nodiscard]] cubey::math::DVec3 to_double(cubey::math::Vec3 value) {
    return {
        static_cast<double>(value.x),
        static_cast<double>(value.y),
        static_cast<double>(value.z),
    };
}

[[nodiscard]] cubey::math::Vec3 to_float(cubey::math::DVec3 value) {
    return {
        static_cast<float>(value.x),
        static_cast<float>(value.y),
        static_cast<float>(value.z),
    };
}

[[nodiscard]] cubey::math::Vec3 position_direction(cubey::math::DVec3 position_m,
                                                   cubey::math::Vec3 fallback) {
    return normalize_or(to_float(normalize_or(position_m, to_double(fallback))), fallback);
}

[[nodiscard]] cubey::math::Vec3 tangent_right_from_up(cubey::math::Vec3 up) {
    cubey::math::Vec3 right = glm::cross(cubey::math::Vec3{0.0F, 0.0F, 1.0F}, up);
    if (glm::length(right) <= 0.000001F) {
        right = glm::cross(cubey::math::Vec3{1.0F, 0.0F, 0.0F}, up);
    }
    return normalize_or(right, cubey::math::Vec3{1.0F, 0.0F, 0.0F});
}

[[nodiscard]] cubey::math::Quat look_rotation(cubey::math::Vec3 forward,
                                              cubey::math::Vec3 up_hint) {
    forward = normalize_or(forward, cubey::math::Vec3{0.0F, 0.0F, -1.0F});
    cubey::math::Vec3 right = glm::cross(forward, up_hint);
    if (glm::length(right) <= 0.000001F) {
        right = tangent_right_from_up(-forward);
    }
    right = normalize_or(right, cubey::math::Vec3{1.0F, 0.0F, 0.0F});
    const cubey::math::Vec3 camera_up =
        normalize_or(glm::cross(right, forward), cubey::math::Vec3{0.0F, 1.0F, 0.0F});
    return glm::normalize(glm::quat_cast(cubey::math::Mat3{right, camera_up, -forward}));
}

[[nodiscard]] cubey::math::Quat rotation_between(cubey::math::Vec3 from, cubey::math::Vec3 to) {
    from = normalize_or(from, cubey::math::Vec3{0.0F, 1.0F, 0.0F});
    to = normalize_or(to, from);
    const float dot_value = std::clamp(glm::dot(from, to), -1.0F, 1.0F);
    if (dot_value > 0.99999F) {
        return cubey::math::identity_quat();
    }
    if (dot_value < -0.99999F) {
        return cubey::math::angle_axis_quat(std::numbers::pi_v<float>, tangent_right_from_up(from));
    }
    return cubey::math::angle_axis_quat(std::acos(dot_value), glm::cross(from, to));
}

[[nodiscard]] float smootherstep(float value) {
    const float x = std::clamp(value, 0.0F, 1.0F);
    return x * x * x * (x * (x * 6.0F - 15.0F) + 10.0F);
}

[[nodiscard]] float orbit_max_latitude_radians() {
    return (std::numbers::pi_v<float> * 0.5F) - kOrbitPoleDeadbandRadians;
}

[[nodiscard]] float orbit_yaw_from_direction(cubey::math::Vec3 direction) {
    direction = normalize_or(direction, cubey::math::Vec3{0.0F, 0.0F, 1.0F});
    if (std::sqrt((direction.x * direction.x) + (direction.z * direction.z)) <= 0.000001F) {
        return 0.0F;
    }
    return std::atan2(direction.x, direction.z);
}

[[nodiscard]] float orbit_latitude_from_direction(cubey::math::Vec3 direction) {
    direction = normalize_or(direction, cubey::math::Vec3{0.0F, 0.0F, 1.0F});
    return std::asin(std::clamp(direction.y, -1.0F, 1.0F));
}

[[nodiscard]] cubey::math::Vec3 orbit_direction_from_yaw_latitude(float yaw_radians,
                                                                  float latitude_radians) {
    const float latitude =
        std::clamp(latitude_radians, -orbit_max_latitude_radians(), orbit_max_latitude_radians());
    const float horizontal = std::cos(latitude);
    return normalize_or(cubey::math::Vec3{std::sin(yaw_radians) * horizontal, std::sin(latitude),
                                          std::cos(yaw_radians) * horizontal},
                        cubey::math::Vec3{0.0F, 0.0F, 1.0F});
}

[[nodiscard]] cubey::math::Vec3 clamped_orbit_direction(cubey::math::Vec3 direction) {
    direction = normalize_or(direction, cubey::math::Vec3{0.0F, 0.0F, 1.0F});
    return orbit_direction_from_yaw_latitude(orbit_yaw_from_direction(direction),
                                             orbit_latitude_from_direction(direction));
}

[[nodiscard]] float terrain_surface_height_m(const PlanetConfig& config,
                                             cubey::math::Vec3 direction) {
    return config.terrain_enabled ? planet_surface_terrain_height_m(config, direction) : 0.0F;
}

[[nodiscard]] float minimum_distance_for_direction(const PlanetConfig& config,
                                                   cubey::math::Vec3 direction) {
    const float surface_height = terrain_surface_height_m(config, direction);
    return std::max(config.radius_m + surface_height + planet_camera_min_altitude_m(config),
                    config.radius_m + 1.0F);
}

[[nodiscard]] float clamped_distance(const PlanetConfig& config, cubey::math::Vec3 direction,
                                     float distance_m) {
    return std::clamp(distance_m, minimum_distance_for_direction(config, direction),
                      planet_camera_max_distance_m(config));
}

[[nodiscard]] cubey::math::Quat orbit_rotation_for_position(cubey::math::Vec3 position_m) {
    const cubey::math::Vec3 up = clamped_orbit_direction(position_m);
    const float yaw = orbit_yaw_from_direction(up);
    const cubey::math::Vec3 forward = -up;
    const cubey::math::Vec3 right =
        normalize_or(cubey::math::Vec3{std::cos(yaw), 0.0F, -std::sin(yaw)},
                     cubey::math::Vec3{1.0F, 0.0F, 0.0F});
    const cubey::math::Vec3 camera_up =
        normalize_or(glm::cross(right, forward), cubey::math::Vec3{0.0F, 1.0F, 0.0F});
    return glm::normalize(glm::quat_cast(cubey::math::Mat3{right, camera_up, -forward}));
}

[[nodiscard]] cubey::math::Quat orbit_rotation_for_position(cubey::math::DVec3 position_m) {
    return orbit_rotation_for_position(position_direction(position_m, {0.0F, 1.0F, 0.0F}));
}

[[nodiscard]] cubey::math::Quat
surface_camera_rotation_for_position(cubey::math::Vec3 position_m, cubey::math::Vec3 heading_hint) {
    const cubey::math::Vec3 up = normalize_or(position_m, cubey::math::Vec3{0.0F, 1.0F, 0.0F});
    cubey::math::Vec3 tangent_forward = normalize_or(heading_hint - up * glm::dot(heading_hint, up),
                                                     glm::cross(up, tangent_right_from_up(up)));
    const float surface_pitch = std::atan(kSurfaceLookDownSlope);
    const cubey::math::Vec3 forward = normalize_or(
        tangent_forward * std::cos(surface_pitch) - up * std::sin(surface_pitch), tangent_forward);
    return look_rotation(forward, up);
}

[[nodiscard]] cubey::math::Quat
surface_camera_rotation_for_position(cubey::math::Vec3 position_m,
                                     cubey::math::Quat reference_rotation) {
    const cubey::math::Vec3 reference_up =
        normalize_or(reference_rotation * cubey::math::Vec3{0.0F, 1.0F, 0.0F},
                     cubey::math::Vec3{0.0F, 1.0F, 0.0F});
    return surface_camera_rotation_for_position(position_m, reference_up);
}

[[nodiscard]] cubey::math::Quat surface_camera_rotation_for_position(cubey::math::Vec3 position_m) {
    const cubey::math::Vec3 up = normalize_or(position_m, cubey::math::Vec3{0.0F, 1.0F, 0.0F});
    return surface_camera_rotation_for_position(position_m,
                                                glm::cross(up, tangent_right_from_up(up)));
}

[[nodiscard]] cubey::math::Quat
surface_camera_rotation_for_position(cubey::math::DVec3 position_m) {
    return surface_camera_rotation_for_position(position_direction(position_m, {0.0F, 1.0F, 0.0F}));
}

[[nodiscard]] cubey::math::DVec3 clamped_position(const PlanetConfig& config,
                                                  cubey::math::DVec3 position_m) {
    const cubey::math::DVec3 direction_d = normalize_or(position_m, {0.0, 0.0, 1.0});
    const cubey::math::Vec3 direction = position_direction(direction_d, {0.0F, 0.0F, 1.0F});
    const double distance = static_cast<double>(
        clamped_distance(config, direction, static_cast<float>(glm::length(position_m))));
    return direction_d * distance;
}

[[nodiscard]] float surface_move_speed_mps(const PlanetConfig& config,
                                           const PlanetCameraState& state) {
    const float altitude = std::max(planet_camera_surface_clearance_m(config, state.position_m),
                                    planet_camera_min_altitude_m(config));
    return std::max({planet_camera_min_altitude_m(config) * 1.6F, altitude * 1.8F,
                     config.radius_m * kSurfaceMovePlanetScaleSpeedRatio});
}

} // namespace

float planet_camera_home_distance_m(const PlanetConfig& config) {
    validate_planet_config(config);
    return std::max(config.radius_m + config.camera_altitude_m, config.radius_m * 1.01F);
}

float planet_camera_min_distance_m(const PlanetConfig& config) {
    return config.radius_m + planet_camera_min_altitude_m(config);
}

float planet_camera_max_distance_m(const PlanetConfig& config) {
    const float home_distance = planet_camera_home_distance_m(config);
    return std::max(home_distance * 8.0F, config.radius_m * 3.0F);
}

float planet_camera_min_altitude_m(const PlanetConfig& config) {
    validate_planet_config(config);
    const float terrain_reference =
        config.terrain_enabled ? config.terrain_height_scale_m * kSurfaceCameraTerrainReferenceRatio
                               : 0.0F;
    return std::max({kSurfaceCameraMinAltitudeM,
                     config.radius_m * kSurfaceCameraMinAltitudeRadiusRatio, terrain_reference});
}

PlanetCameraSurfaceMetrics planet_camera_surface_metrics(const PlanetConfig& config,
                                                         cubey::math::DVec3 position_m) {
    validate_planet_config(config);
    const float radius = static_cast<float>(glm::length(position_m));
    const cubey::math::Vec3 direction = position_direction(position_m, {0.0F, 0.0F, 1.0F});
    const float terrain_height = terrain_surface_height_m(config, direction);
    return {
        .radius_m = radius,
        .datum_altitude_m = std::max(radius - config.radius_m, 0.0F),
        .terrain_height_m = terrain_height,
        .clearance_m = radius - (config.radius_m + terrain_height),
    };
}

float planet_camera_surface_height_m(const PlanetConfig& config, cubey::math::DVec3 position_m) {
    return planet_camera_surface_metrics(config, position_m).terrain_height_m;
}

float planet_camera_surface_clearance_m(const PlanetConfig& config, cubey::math::DVec3 position_m) {
    return planet_camera_surface_metrics(config, position_m).clearance_m;
}

float planet_surface_camera_blend_from_clearance(const PlanetConfig& config, float clearance_m) {
    validate_planet_config(config);
    const float altitude_m = std::max(clearance_m, 0.0F);
    const float full_surface_altitude =
        std::max(planet_camera_min_altitude_m(config) * 2.0F, config.radius_m * 0.04F);
    const float orbit_altitude = std::max(full_surface_altitude * 4.0F, config.radius_m * 0.30F);
    if (altitude_m >= orbit_altitude) {
        return 0.0F;
    }
    if (altitude_m <= full_surface_altitude) {
        return 1.0F;
    }
    const float t = (orbit_altitude - altitude_m) / (orbit_altitude - full_surface_altitude);
    return smootherstep(t);
}

float planet_camera_distance_m(const PlanetCameraState& state) {
    return static_cast<float>(glm::length(state.position_m));
}

cubey::math::Vec3 planet_camera_position_float_m(const PlanetCameraState& state) {
    return to_float(state.position_m);
}

PlanetCameraState planet_camera_home_state(const PlanetConfig& config, float base_yaw_radians,
                                           float base_pitch_radians) {
    validate_planet_config(config);
    const cubey::math::Quat initial_rotation =
        cubey::math::angle_axis_quat(base_yaw_radians, {0.0F, 1.0F, 0.0F}) *
        cubey::math::angle_axis_quat(base_pitch_radians, {1.0F, 0.0F, 0.0F});
    PlanetCameraState state{
        .position_m =
            to_double(initial_rotation *
                      cubey::math::Vec3{0.0F, 0.0F, planet_camera_home_distance_m(config)}),
    };
    state.surface_rotation = surface_camera_rotation_for_position(state.position_m);
    return state;
}

PlanetCameraState planet_camera_initial_state_from_run_config(const PlanetConfig& config,
                                                              const RunConfig& run_config,
                                                              float base_yaw_radians,
                                                              float base_pitch_radians) {
    PlanetCameraState state =
        planet_camera_home_state(config, base_yaw_radians, base_pitch_radians);
    if (run_config.planet.camera_mode.empty() || run_config.planet.camera_mode == "orbit") {
        return state;
    }
    if (run_config.planet.camera_mode == "surface") {
        const float requested_altitude =
            run_config_float_is_set(run_config.planet.camera_altitude_m)
                ? run_config.planet.camera_altitude_m
                : config.camera_altitude_m;
        const float surface_altitude =
            std::min(requested_altitude, std::max(planet_camera_min_altitude_m(config) * 1.6F,
                                                  config.radius_m * 0.025F));
        planet_camera_set_distance(state, config, config.radius_m + surface_altitude);
        planet_camera_reset_surface_view(state, config);
        return state;
    }
    return state;
}

void planet_camera_set_distance(PlanetCameraState& state, const PlanetConfig& config,
                                float distance_m) {
    const cubey::math::DVec3 direction_d =
        normalize_or(state.position_m, cubey::math::DVec3{0.0, 0.0, 1.0});
    const cubey::math::Vec3 direction = position_direction(direction_d, {0.0F, 0.0F, 1.0F});
    state.position_m =
        direction_d * static_cast<double>(clamped_distance(config, direction, distance_m));
}

void planet_camera_orbit_rotate(PlanetCameraState& state, const PlanetConfig& config,
                                float yaw_delta_radians, float latitude_delta_radians) {
    if (yaw_delta_radians == 0.0F && latitude_delta_radians == 0.0F) {
        return;
    }

    const double distance = glm::length(state.position_m);
    const cubey::math::Vec3 direction = position_direction(state.position_m, {0.0F, 0.0F, 1.0F});
    const float yaw = orbit_yaw_from_direction(direction) + yaw_delta_radians;
    const float latitude = orbit_latitude_from_direction(direction) + latitude_delta_radians;
    const cubey::math::Vec3 position = orbit_direction_from_yaw_latitude(yaw, latitude);
    state.position_m = clamped_position(config, to_double(position) * distance);
    state.surface_rotation_active = false;
}

void planet_camera_zoom_by_scroll(PlanetCameraState& state, const PlanetConfig& config,
                                  double scroll_y) {
    if (scroll_y == 0.0) {
        return;
    }
    const float factor = std::pow(kZoomBase, static_cast<float>(scroll_y));
    const float surface_height = planet_camera_surface_height_m(config, state.position_m);
    const float current_clearance =
        std::max(planet_camera_surface_clearance_m(config, state.position_m),
                 planet_camera_min_altitude_m(config));
    planet_camera_set_distance(state, config,
                               config.radius_m + surface_height + (current_clearance * factor));
}

void planet_camera_orbit_drag(PlanetCameraState& state, const PlanetConfig& config,
                              double delta_x_px, double delta_y_px) {
    if (delta_x_px == 0.0 && delta_y_px == 0.0) {
        return;
    }

    planet_camera_orbit_rotate(state, config,
                               -static_cast<float>(delta_x_px) * kDragRadiansPerPixel,
                               static_cast<float>(delta_y_px) * kDragRadiansPerPixel);
}

void planet_camera_surface_look_rotate(PlanetCameraState& state, const PlanetConfig& config,
                                       float yaw_delta_radians, float pitch_delta_radians) {
    if (yaw_delta_radians == 0.0F && pitch_delta_radians == 0.0F) {
        return;
    }
    if (!state.surface_rotation_active) {
        state.surface_rotation = make_planet_camera_transform(config, state).rotation;
        state.surface_rotation_active = true;
    }

    const cubey::math::Vec3 up =
        position_direction(state.position_m, cubey::math::Vec3{0.0F, 1.0F, 0.0F});
    const cubey::math::Quat yaw = cubey::math::angle_axis_quat(yaw_delta_radians, up);
    cubey::math::Quat candidate = glm::normalize(yaw * state.surface_rotation);
    const cubey::math::Vec3 right =
        normalize_or(candidate * cubey::math::Vec3{1.0F, 0.0F, 0.0F}, tangent_right_from_up(up));
    const cubey::math::Quat pitch = cubey::math::angle_axis_quat(pitch_delta_radians, right);
    const cubey::math::Quat pitched = glm::normalize(pitch * candidate);
    const cubey::math::Vec3 forward =
        glm::normalize(pitched * cubey::math::Vec3{0.0F, 0.0F, -1.0F});
    const float forward_up = glm::dot(forward, up);
    if (forward_up >= kSurfaceMinForwardUpDot && forward_up <= kSurfaceMaxForwardUpDot) {
        candidate = pitched;
    }
    state.surface_rotation = glm::normalize(candidate);
    state.position_m = clamped_position(config, state.position_m);
}

void planet_camera_surface_look_drag(PlanetCameraState& state, const PlanetConfig& config,
                                     double delta_x_px, double delta_y_px) {
    if (delta_x_px == 0.0 && delta_y_px == 0.0) {
        return;
    }

    planet_camera_surface_look_rotate(
        state, config, -static_cast<float>(delta_x_px) * kSurfaceLookRadiansPerPixel,
        -static_cast<float>(delta_y_px) * kSurfaceLookRadiansPerPixel);
}

bool planet_camera_surface_move(PlanetCameraState& state, const PlanetConfig& config, float forward,
                                float right, double delta_seconds) {
    if (delta_seconds <= 0.0 || (forward == 0.0F && right == 0.0F) ||
        planet_surface_camera_blend_from_clearance(
            config, planet_camera_surface_clearance_m(config, state.position_m)) < 0.35F) {
        return false;
    }
    if (!state.surface_rotation_active) {
        state.surface_rotation = make_planet_camera_transform(config, state).rotation;
        state.surface_rotation_active = true;
    }

    cubey::math::Vec2 input{right, forward};
    if (glm::length(input) > 1.0F) {
        input = glm::normalize(input);
    }

    const cubey::math::Vec3 old_up =
        position_direction(state.position_m, cubey::math::Vec3{0.0F, 1.0F, 0.0F});
    const cubey::math::Vec3 camera_forward =
        glm::normalize(state.surface_rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F});
    const cubey::math::Vec3 tangent_forward =
        normalize_or(camera_forward - old_up * glm::dot(camera_forward, old_up),
                     glm::cross(old_up, tangent_right_from_up(old_up)));
    const cubey::math::Vec3 camera_right =
        glm::normalize(state.surface_rotation * cubey::math::Vec3{1.0F, 0.0F, 0.0F});
    const cubey::math::Vec3 tangent_right = normalize_or(
        camera_right - old_up * glm::dot(camera_right, old_up), tangent_right_from_up(old_up));
    const cubey::math::Vec3 tangent_motion = tangent_forward * input.y + tangent_right * input.x;
    if (glm::length(tangent_motion) <= 0.000001F) {
        return false;
    }

    const float clearance = std::max(planet_camera_surface_clearance_m(config, state.position_m),
                                     planet_camera_min_altitude_m(config));
    const float step_m = surface_move_speed_mps(config, state) * static_cast<float>(delta_seconds);
    const cubey::math::DVec3 moved_direction = normalize_or(
        state.position_m + to_double(glm::normalize(tangent_motion)) * static_cast<double>(step_m),
        state.position_m);
    const float moved_surface_height = planet_camera_surface_height_m(config, moved_direction);
    const cubey::math::DVec3 moved_position =
        moved_direction * static_cast<double>(config.radius_m + moved_surface_height + clearance);
    const cubey::math::Vec3 new_up = position_direction(moved_position, old_up);
    state.position_m = clamped_position(config, moved_position);
    state.surface_rotation =
        glm::normalize(rotation_between(old_up, new_up) * state.surface_rotation);
    return true;
}

void planet_camera_update_surface_mode(PlanetCameraState& state, const PlanetConfig& config) {
    const float blend = planet_surface_camera_blend_from_clearance(
        config, planet_camera_surface_clearance_m(config, state.position_m));
    if (blend <= 0.10F) {
        state.surface_rotation_active = false;
    }
}

void planet_camera_reset_surface_view(PlanetCameraState& state, const PlanetConfig& config) {
    state.position_m = clamped_position(config, state.position_m);
    state.surface_rotation = surface_camera_rotation_for_position(state.position_m);
    state.surface_rotation_active =
        planet_surface_camera_blend_from_clearance(
            config, planet_camera_surface_clearance_m(config, state.position_m)) > 0.10F;
}

cubey::Transform3D make_planet_camera_transform(const PlanetConfig& config,
                                                PlanetCameraState state) {
    validate_planet_config(config);
    state.position_m = clamped_position(config, state.position_m);
    const cubey::Transform3D orbit_transform{
        .translation = to_float(state.position_m),
        .rotation = orbit_rotation_for_position(state.position_m),
    };
    const float blend = planet_surface_camera_blend_from_clearance(
        config, planet_camera_surface_clearance_m(config, state.position_m));
    if (blend <= 0.0F) {
        return orbit_transform;
    }

    const cubey::math::Quat surface_rotation =
        state.surface_rotation_active
            ? state.surface_rotation
            : surface_camera_rotation_for_position(state.position_m, orbit_transform.rotation);
    return {
        .translation = orbit_transform.translation,
        .rotation = glm::normalize(glm::slerp(orbit_transform.rotation, surface_rotation, blend)),
    };
}

} // namespace cubey::projects::planet
