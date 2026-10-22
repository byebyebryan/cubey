#pragma once

#include <cubey/core/math.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cubey::projects::sdf {

enum class Operation : std::uint32_t {
    Fill = 1,
    Remove = 2
};

struct BoxEdit {
    math::Vec3 center{0.0F};
    math::Quat rotation = math::identity_quat();
    math::Vec3 half_extent{0.2F}; // Positive box core; all zero + rounding > 0 is an exact sphere.
    float rounding = 0.0005F;
    Operation operation = Operation::Fill;
};

// Three explicit vec4 lanes; no host vec3/quaternion memory is copied to GLSL.
struct alignas(16) GpuEdit {
    math::Vec4 center_operation{};
    math::Vec4 inverse_rotation{};
    math::Vec4 extent_rounding{};
};
static_assert(sizeof(GpuEdit) == 48);
static_assert(offsetof(GpuEdit, inverse_rotation) == 16);
static_assert(offsetof(GpuEdit, extent_rounding) == 32);

struct Recipe {
    std::vector<BoxEdit> edits;
    std::string generator = "explicit";
    std::uint32_t seed = 0;
};

[[nodiscard]] GpuEdit gpu_edit(const BoxEdit& edit);
[[nodiscard]] std::vector<GpuEdit> gpu_edits(std::span<const BoxEdit> edits);
[[nodiscard]] float box_distance(math::Vec3 point, math::Vec3 half_extent, float rounding);
[[nodiscard]] float edit_distance(math::Vec3 point, const BoxEdit& edit);
[[nodiscard]] float recipe_distance(math::Vec3 point, std::span<const BoxEdit> edits);
[[nodiscard]] Recipe make_recipe(std::string_view scene, std::uint32_t seed, std::uint32_t count);
void validate_carve_recipe(const Recipe& recipe);
[[nodiscard]] std::uint64_t recipe_hash(const Recipe& recipe);
[[nodiscard]] std::size_t construction_prefix(std::size_t count, double seconds, double duration);
void validate_edit(const BoxEdit& edit);
[[nodiscard]] std::string serialize_recipe(const Recipe& recipe);
[[nodiscard]] Recipe parse_recipe(std::string_view json);
void save_recipe(const std::filesystem::path& path, const Recipe& recipe);
[[nodiscard]] Recipe load_recipe(const std::filesystem::path& path);

} // namespace cubey::projects::sdf
