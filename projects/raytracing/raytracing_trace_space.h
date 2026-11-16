#pragma once
#include <cubey/core/math.h>

namespace cubey::projects::raytracing {
// A fixed translation shared by TLAS instances, shading transforms and rays.
// Geometry, directions and physical scene/camera settings stay in their own
// spaces. Keeping this frame fixed also preserves AS reuse during camera edits.
struct TraceSpace {
    math::Vec3 world_origin{0};

    [[nodiscard]] math::Mat4 from_world(math::Mat4 transform) const {
        transform[3] -= math::Vec4(world_origin, 0);
        return transform;
    }
    [[nodiscard]] math::Vec3 from_world(math::Vec3 point) const {
        return point - world_origin;
    }
    [[nodiscard]] math::Vec3 to_world(math::Vec3 point) const {
        return point + world_origin;
    }
};
} // namespace cubey::projects::raytracing
