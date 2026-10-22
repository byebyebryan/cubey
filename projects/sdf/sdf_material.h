#pragma once
#include "sdf_authored.h"

namespace cubey::projects::sdf {
struct NoiseGradient {
    float value = 0;
    math::Vec3 gradient{0};
};
struct MaterialSample {
    SurfaceMaterial material;
    math::Vec3 height_gradient{0};
};
[[nodiscard]] NoiseGradient material_noise(math::Vec3 point, std::uint32_t seed);
[[nodiscard]] MaterialSample sample_material(const SurfaceMaterial& base,
                                             const MaterialPattern& pattern, math::Vec3 point,
                                             float footprint = 0);
[[nodiscard]] math::Vec3 material_shading_normal(math::Vec3 geometric_normal,
                                                 math::Vec3 height_gradient);
void validate_material_pattern(const MaterialPattern& pattern);
} // namespace cubey::projects::sdf
