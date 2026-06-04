#pragma once

#include "ocean_mesh.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cubey::projects::ocean {

inline constexpr float kOceanMetersPerKilometer = 1000.0F;
inline constexpr float kOceanMinimumCameraAltitudeMeters = 0.01F;

struct OceanHorizonDiagnostics {
    float camera_altitude_m = 0.0F;
    float planet_radius_m = 0.0F;
    float horizon_distance_m = 0.0F;
    float required_half_extent_m = 0.0F;
    float mesh_half_extent_m = 0.0F;
    float coverage_ratio = 0.0F;
    float near_cell_size_m = 0.0F;
    float far_cell_size_m = 0.0F;
};

[[nodiscard]] inline float ocean_planet_radius_m(float bottom_radius_km) {
    if (!std::isfinite(bottom_radius_km) || bottom_radius_km <= 0.0F) {
        throw std::runtime_error("ocean planet radius must be positive");
    }
    return bottom_radius_km * kOceanMetersPerKilometer;
}

[[nodiscard]] inline float ocean_camera_altitude_m(float camera_y_m, float water_datum_y_m) {
    if (!std::isfinite(camera_y_m) || !std::isfinite(water_datum_y_m)) {
        throw std::runtime_error("ocean camera altitude inputs must be finite");
    }
    return std::max(camera_y_m - water_datum_y_m, kOceanMinimumCameraAltitudeMeters);
}

[[nodiscard]] inline float ocean_horizon_distance_m(float planet_radius_m,
                                                    float camera_altitude_m) {
    if (!std::isfinite(planet_radius_m) || !std::isfinite(camera_altitude_m) ||
        planet_radius_m <= 0.0F || camera_altitude_m < 0.0F) {
        throw std::runtime_error("ocean horizon distance inputs must be valid");
    }
    const double radius = static_cast<double>(planet_radius_m);
    const double altitude = static_cast<double>(camera_altitude_m);
    return static_cast<float>(std::sqrt(std::max(0.0, (radius + altitude) *
                                                          (radius + altitude) -
                                                          radius * radius)));
}

[[nodiscard]] inline float ocean_required_horizon_half_extent_m(float horizon_distance_m,
                                                                float safety_margin) {
    if (!std::isfinite(horizon_distance_m) || !std::isfinite(safety_margin) ||
        horizon_distance_m < 0.0F || safety_margin <= 0.0F) {
        throw std::runtime_error("ocean horizon extent inputs must be valid");
    }
    return horizon_distance_m * safety_margin;
}

[[nodiscard]] inline float ocean_horizon_coverage_ratio(float mesh_half_extent_m,
                                                        float required_half_extent_m) {
    if (!std::isfinite(mesh_half_extent_m) || !std::isfinite(required_half_extent_m) ||
        mesh_half_extent_m < 0.0F || required_half_extent_m < 0.0F) {
        throw std::runtime_error("ocean horizon coverage inputs must be valid");
    }
    if (required_half_extent_m <= 0.001F) {
        return 1.0F;
    }
    return mesh_half_extent_m / required_half_extent_m;
}

[[nodiscard]] inline OceanHorizonDiagnostics
ocean_horizon_diagnostics(const OceanConfig& config,
                          float camera_y_m,
                          float water_datum_y_m,
                          float planet_radius_m,
                          float safety_margin) {
    const float camera_altitude =
        ocean_camera_altitude_m(camera_y_m, water_datum_y_m);
    const float horizon_distance = ocean_horizon_distance_m(planet_radius_m, camera_altitude);
    const float required_half_extent =
        ocean_required_horizon_half_extent_m(horizon_distance, safety_margin);
    return {
        .camera_altitude_m = camera_altitude,
        .planet_radius_m = planet_radius_m,
        .horizon_distance_m = horizon_distance,
        .required_half_extent_m = required_half_extent,
        .mesh_half_extent_m = config.mesh_extent,
        .coverage_ratio = ocean_horizon_coverage_ratio(config.mesh_extent, required_half_extent),
        .near_cell_size_m = ocean_mesh_near_cell_size(config),
        .far_cell_size_m = ocean_mesh_level_cell_size(config, config.mesh_lod_levels - 1U),
    };
}

} // namespace cubey::projects::ocean
