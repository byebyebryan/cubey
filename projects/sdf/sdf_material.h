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
struct MarbleDetail {
    const char* name;
    float scale, frequency, width, warp, grain;
};
inline constexpr std::array<MarbleDetail, 4> marble_details{{
    {"Original", 4.5F, 5.5F, .08F, 5, 220},
    {"Balanced", 6, 7.25F, .07F, 5.5F, 280},
    {"Denser", 9, 11, .055F, 7, 360},
    {"Fine", 12, 12, .05F, 8.5F, 360},
}};
[[nodiscard]] int marble_detail_index(const MaterialPattern& pattern);
void apply_marble_detail(MaterialPattern& pattern, std::size_t index);
[[nodiscard]] NoiseGradient material_noise(math::Vec3 point, std::uint32_t seed);
[[nodiscard]] MaterialSample sample_material(const SurfaceMaterial& base,
                                             const MaterialPattern& pattern, math::Vec3 point,
                                             float footprint = 0);
[[nodiscard]] math::Vec3 material_shading_normal(math::Vec3 geometric_normal,
                                                 math::Vec3 height_gradient);
void validate_material_pattern(const MaterialPattern& pattern);
} // namespace cubey::projects::sdf
