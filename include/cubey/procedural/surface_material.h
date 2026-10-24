#pragma once
#include <array>
#include <cstddef>
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
// V2 library parameters have their own checked GPU layout. Legacy pattern and
// optics bytes above retain their original meanings and stride.
struct alignas(16) WoodParameters {
    float ring_frequency = 32, distortion = .45F, grain_frequency = 220, pore_strength = .35F;
    bool operator==(const WoodParameters&) const = default;
};
struct alignas(16) RustParameters {
    float coverage = .6F, patch_frequency = 12, edge_breakup = .35F, pit_strength = .6F;
    bool operator==(const RustParameters&) const = default;
};
struct alignas(16) WeaveParameters {
    float yarn_spacing = .012F, yarn_width = .72F, irregularity = .15F, fiber_strength = .25F;
    bool operator==(const WeaveParameters&) const = default;
};
struct alignas(16) PaperParameters {
    float fiber_frequency = 400, directionality = .65F, fleck_strength = .2F, fiber_strength = .5F;
    bool operator==(const PaperParameters&) const = default;
};
struct alignas(16) SurfaceLibrary {
    math::Vec4 settings{0, 7, 1, .0005F}; // kind: none/wood/rust/weave/paper, seed, scale, relief
    math::Vec4 secondary{.12F, .045F, .012F, 0}; // secondary linear color, reserved zero
    WoodParameters wood;
    RustParameters rust;
    WeaveParameters weave;
    PaperParameters paper;
    math::Vec4 rust_roughness{.24F, .95F, .08F, 0}; // steel, oxide, variation, reserved zero
    math::Vec4 sheen{0, 0, 0, .5F}; // linear sheen color, perceptual sheen roughness
    math::Vec4 optical_options{0};  // x: opt-in rough glass; yzw reserved zero
    bool operator==(const SurfaceLibrary&) const = default;
};
static_assert(sizeof(SurfaceLibrary) == 144);
static_assert(offsetof(SurfaceLibrary, wood) == 32);
static_assert(offsetof(SurfaceLibrary, rust_roughness) == 96);
static_assert(offsetof(SurfaceLibrary, optical_options) == 128);
[[nodiscard]] bool default_surface_library(const SurfaceLibrary& library);
void validate_surface_library(const SurfaceLibrary& library);
[[nodiscard]] bool default_optics(const MaterialOptics& optics);
void validate_material_optics(const MaterialOptics& optics);

struct NoiseGradient {
    float value = 0;
    math::Vec3 gradient{0};
};
struct MaterialSample {
    SurfaceMaterial material;
    math::Vec3 height_gradient{0};
    float metallic = -1; // negative: inherit constant optics (legacy path)
    math::Vec4 sheen{0};
    float rough_glass = 0;
    float height = 0; // library relief in material units, for derivative inspection
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
[[nodiscard]] MaterialSample sample_library_surface(const SurfaceMaterial&, const SurfaceLibrary&,
                                                    math::Vec3 point, float footprint = 0);
[[nodiscard]] MaterialSample sample_surface_domain(const SurfaceMaterial&, const MaterialPattern&,
                                                   const SurfaceLibrary&, const math::Mat4&,
                                                   math::Vec3 point, float footprint = 0,
                                                   bool patterns = true);
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
