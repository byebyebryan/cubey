#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace cubey::projects::gltf_viewer {

struct GltfViewerLandscapeDepthRange {
    float near_m, far_m;
};

// Distance bounds enclose the actual retained triangles about the orbit pivot.
// Subtract the ENTIRE allowed camera orbit, then account for the frustum corner
// angle: Euclidean distance alone is not a lower bound for camera-space Z.
// The factor of two leaves guard for CPU/GPU surface-evaluation roundoff.
// Independent of yaw/pitch and current zoom, so a movie cannot pump its range.
[[nodiscard]] inline GltfViewerLandscapeDepthRange
gltf_viewer_landscape_depth_range(float minimum_distance_m, float maximum_distance_m,
                                  float orbit_radius_m, float vertical_fov_radians, float aspect,
                                  std::optional<float> requested_near = {}) {
    if (!std::isfinite(minimum_distance_m) || !std::isfinite(maximum_distance_m) ||
        !std::isfinite(orbit_radius_m) || !std::isfinite(vertical_fov_radians) ||
        !std::isfinite(aspect) || minimum_distance_m <= orbit_radius_m || orbit_radius_m < 0 ||
        maximum_distance_m < minimum_distance_m || vertical_fov_radians <= 0 ||
        vertical_fov_radians >= 3.14159265F || aspect <= 0)
        throw std::runtime_error("landscape depth fit requires a disjoint, finite camera orbit");
    const double tangent = std::tan(double(vertical_fov_radians) * .5);
    const double corner_cosine = 1.0 / std::sqrt(1.0 + tangent * tangent * (1.0 + aspect * aspect));
    const float safe_near = float((minimum_distance_m - orbit_radius_m) * corner_cosine * .5);
    if (safe_near < .01F ||
        (requested_near && (!std::isfinite(*requested_near) || *requested_near < .01F ||
                            *requested_near > safe_near)))
        throw std::runtime_error("requested landscape near plane exceeds the conservative fit");
    return {requested_near.value_or(safe_near), (maximum_distance_m + orbit_radius_m) * 1.05F};
}

} // namespace cubey::projects::gltf_viewer
