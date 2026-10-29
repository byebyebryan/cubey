#include "sdf_prepared_cones.h"
#include <cmath>

namespace cubey::projects::sdf {
bool PreparedCones::matches(const AuthoredScene& scene) const {
    // This non-persistent type fixes the format for a renderer lifetime. The
    // per-entry version tag is also checked by the GPU before using coefficients.
    return instruction_count == scene.program.size() && entries.size() == instruction_count &&
           geometry_hash == authored_geometry_hash(scene);
}
PreparedCones prepare_cones(const AuthoredScene& scene) {
    PreparedCones result;
    result.geometry_hash = authored_geometry_hash(scene);
    result.instruction_count = scene.program.size();
    result.entries.resize(scene.program.size(), math::Vec4{0});
    for (std::size_t i = 0; i < scene.program.size(); ++i) {
        const auto& node = scene.program[i];
        if (node.center_code.w != static_cast<float>(ShapeCode::RoundCone))
            continue;
        const float bottom = node.parameters.x, top = node.parameters.y;
        const float height = 2 * node.parameters.z;
        if (!(height > 0) || std::abs(bottom - top) >= height)
            continue;
        const float slope = (top - bottom) / height;
        const float radial = std::sqrt(std::max(0.F, 1 - slope * slope));
        const float threshold = height * radial;
        if (std::isfinite(slope) && std::isfinite(radial) && std::isfinite(threshold))
            result.entries[i] = {slope, radial, threshold, static_cast<float>(PreparedCones::version)};
    }
    return result;
}
} // namespace cubey::projects::sdf
