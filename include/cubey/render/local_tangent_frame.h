#pragma once

#include <cubey/core/math.h>

#include <cmath>
#include <stdexcept>

namespace cubey::render {

inline constexpr float kLocalTangentDefaultPlanetRadiusMeters = 6371000.0F;
inline constexpr float kLocalTangentBasisTolerance = 0.001F;

struct LocalTangentFrame {
    math::DVec3 world_origin_m{0.0, 0.0, 0.0};
    math::Vec3 right{1.0F, 0.0F, 0.0F};
    math::Vec3 up{0.0F, 1.0F, 0.0F};
    math::Vec3 forward{0.0F, 0.0F, 1.0F};
    float planet_radius_m = kLocalTangentDefaultPlanetRadiusMeters;
    float water_datum_m = 0.0F;
};

[[nodiscard]] inline bool local_tangent_is_finite(math::Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] inline bool local_tangent_is_finite(math::DVec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

inline void validate_local_tangent_frame(const LocalTangentFrame& frame) {
    if (!local_tangent_is_finite(frame.world_origin_m) ||
        !local_tangent_is_finite(frame.right) || !local_tangent_is_finite(frame.up) ||
        !local_tangent_is_finite(frame.forward) || !std::isfinite(frame.planet_radius_m) ||
        !std::isfinite(frame.water_datum_m) || frame.planet_radius_m <= 0.0F) {
        throw std::runtime_error("local tangent frame values must be finite and positive");
    }

    const float right_length = glm::length(frame.right);
    const float up_length = glm::length(frame.up);
    const float forward_length = glm::length(frame.forward);
    if (std::abs(right_length - 1.0F) > kLocalTangentBasisTolerance ||
        std::abs(up_length - 1.0F) > kLocalTangentBasisTolerance ||
        std::abs(forward_length - 1.0F) > kLocalTangentBasisTolerance) {
        throw std::runtime_error("local tangent frame basis vectors must be unit length");
    }

    if (std::abs(glm::dot(frame.right, frame.up)) > kLocalTangentBasisTolerance ||
        std::abs(glm::dot(frame.right, frame.forward)) > kLocalTangentBasisTolerance ||
        std::abs(glm::dot(frame.up, frame.forward)) > kLocalTangentBasisTolerance) {
        throw std::runtime_error("local tangent frame basis vectors must be orthogonal");
    }
}

[[nodiscard]] inline math::DVec3 local_tangent_frame_axis(math::Vec3 axis) {
    return {static_cast<double>(axis.x), static_cast<double>(axis.y),
            static_cast<double>(axis.z)};
}

[[nodiscard]] inline math::Vec3 local_tangent_world_to_local_m(
    const LocalTangentFrame& frame, math::DVec3 world_position_m) {
    validate_local_tangent_frame(frame);
    if (!local_tangent_is_finite(world_position_m)) {
        throw std::runtime_error("local tangent world position must be finite");
    }

    const math::DVec3 relative = world_position_m - frame.world_origin_m;
    return {
        static_cast<float>(glm::dot(relative, local_tangent_frame_axis(frame.right))),
        static_cast<float>(glm::dot(relative, local_tangent_frame_axis(frame.up))),
        static_cast<float>(glm::dot(relative, local_tangent_frame_axis(frame.forward))),
    };
}

[[nodiscard]] inline math::DVec3 local_tangent_local_to_world_m(
    const LocalTangentFrame& frame, math::Vec3 local_position_m) {
    validate_local_tangent_frame(frame);
    if (!local_tangent_is_finite(local_position_m)) {
        throw std::runtime_error("local tangent local position must be finite");
    }

    return frame.world_origin_m + local_tangent_frame_axis(frame.right) *
                                      static_cast<double>(local_position_m.x) +
           local_tangent_frame_axis(frame.up) * static_cast<double>(local_position_m.y) +
           local_tangent_frame_axis(frame.forward) * static_cast<double>(local_position_m.z);
}

[[nodiscard]] inline math::Vec3 local_tangent_camera_relative_position_m(
    const LocalTangentFrame& frame,
    math::DVec3 world_position_m,
    math::DVec3 camera_world_position_m) {
    validate_local_tangent_frame(frame);
    if (!local_tangent_is_finite(world_position_m) ||
        !local_tangent_is_finite(camera_world_position_m)) {
        throw std::runtime_error("local tangent camera-relative inputs must be finite");
    }

    const math::DVec3 relative = world_position_m - camera_world_position_m;
    return {
        static_cast<float>(glm::dot(relative, local_tangent_frame_axis(frame.right))),
        static_cast<float>(glm::dot(relative, local_tangent_frame_axis(frame.up))),
        static_cast<float>(glm::dot(relative, local_tangent_frame_axis(frame.forward))),
    };
}

[[nodiscard]] inline float local_tangent_height_above_datum_m(
    const LocalTangentFrame& frame, math::DVec3 world_position_m) {
    return local_tangent_world_to_local_m(frame, world_position_m).y - frame.water_datum_m;
}

} // namespace cubey::render
