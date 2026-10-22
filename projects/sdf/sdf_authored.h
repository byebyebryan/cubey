#pragma once
#include "sdf_recipe.h"
#include <array>
#include <optional>

namespace cubey::projects::sdf {
enum class ShapeCode : std::uint32_t {
    Box = 1,
    Sphere = 2,
    Capsule = 3,
    Cylinder = 4,
    Union = 10,
    Subtract = 11,
    SmoothUnion = 12
};
// Postfix program. Operators consume two stack entries; leaves push one.
// Transforms are flattened to world space, with distance-preserving uniform scale.
struct alignas(16) SceneInstruction {
    math::Vec4 center_code{};
    math::Vec4 inverse_rotation{};
    math::Vec4 parameters{};
    math::Vec4 settings{}; // scale, blend radius, material index, cut material / blend flag
};
static_assert(sizeof(SceneInstruction) == 64);
struct alignas(16) SurfaceMaterial {
    math::Vec4 base_roughness{0.5F, 0.5F, 0.5F, 0.5F};
    math::Vec4 emission{0};
};
static_assert(sizeof(SurfaceMaterial) == 32);
struct AuthoredStep {
    std::string node, label;
    double seconds = 0.65;
    std::optional<SceneInstruction> cutter;
};
struct AuthoredScene {
    std::string name;
    std::string document; // Explicit named tree; retained for faithful persistence.
    std::vector<SceneInstruction> program;
    std::vector<std::string> part_names; // One name per instruction, including groups.
    std::vector<std::string> material_names;
    std::vector<SurfaceMaterial> materials;
    std::vector<AuthoredStep> history;
    std::size_t leaf_count = 0;
};
struct SurfaceSample {
    float distance = 1000;
    SurfaceMaterial material;
    std::uint32_t part = 0;
};
[[nodiscard]] AuthoredScene parse_authored_scene(std::string_view text);
[[nodiscard]] AuthoredScene load_authored_scene(const std::filesystem::path& path);
[[nodiscard]] std::string serialize_authored_scene(const AuthoredScene& scene);
void save_authored_scene(const std::filesystem::path& path, const AuthoredScene& scene);
[[nodiscard]] std::uint64_t authored_geometry_hash(const AuthoredScene& scene);
[[nodiscard]] std::uint64_t authored_appearance_hash(const AuthoredScene& scene);
[[nodiscard]] float authored_distance(math::Vec3 point, const AuthoredScene& scene);
[[nodiscard]] SurfaceSample authored_surface(math::Vec3 point, const AuthoredScene& scene);
// Header followed by explicit vec4 instructions; suitable for a storage buffer.
[[nodiscard]] std::vector<std::uint8_t> authored_program_bytes(const AuthoredScene* scene);
} // namespace cubey::projects::sdf
