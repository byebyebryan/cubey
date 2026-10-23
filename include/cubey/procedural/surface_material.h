#pragma once
#include <array>
#include <cstdint>
#include <cubey/core/math.h>

namespace cubey::procedural {
struct alignas(16) SurfaceMaterial {
    math::Vec4 base_roughness{0.5F, 0.5F, 0.5F, 0.5F};
    math::Vec4 emission{0};
};
static_assert(sizeof(SurfaceMaterial) == 32);
struct alignas(16) MaterialPattern {
    math::Vec4 settings{
        0, 7, 6, .5F}; // kind (0 plain, 1 stone, 2 marble, 3 grain), seed, scale, color strength
    math::Vec4 secondary{.2F, .18F, .15F, .08F}; // linear RGB, roughness variation
    math::Vec4 veins{1, .3F, .2F, 5};            // direction, frequency
    math::Vec4 detail{.0005F, 160, .10F, 5};     // bump height, grain scale, vein width, warp
    math::Vec4 offset{0}; // fixed model-space domain offset XYZ, marble body color variation
};
static_assert(sizeof(MaterialPattern) == 80);
struct alignas(16) MaterialOptics {
    math::Vec4 settings{0, 0, 1.5F, 1}; // metallic, transmission, IOR, attenuation distance
    math::Vec4 attenuation{1, 1, 1,
                           0}; // RGB transmittance, diffuse roughness (0 Lambert, 1 sigma=90deg)
};
static_assert(sizeof(MaterialOptics) == 32);
[[nodiscard]] bool default_optics(const MaterialOptics& optics);
void validate_material_optics(const MaterialOptics& optics);

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
void validate_material_domain(const math::Mat4& material_from_object);
[[nodiscard]] MaterialSample sample_material_domain(const SurfaceMaterial& base,
                                                    const MaterialPattern& pattern,
                                                    const math::Mat4& material_from_object,
                                                    math::Vec3 object_point,
                                                    float object_footprint = 0);
struct OpticalInterface {
    math::Vec3 reflected, transmitted;
    float fresnel;
    bool tir;
};
[[nodiscard]] OpticalInterface optical_interface(math::Vec3 incident, math::Vec3 outward,
                                                 bool inside, float ior);
[[nodiscard]] math::Vec3 optical_absorption(const MaterialOptics& optics, float distance);
[[nodiscard]] float rough_diffuse_factor(float nl, float nv, float lv, float roughness);
} // namespace cubey::procedural
