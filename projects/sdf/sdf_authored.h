#pragma once
#include "sdf_recipe.h"
#include <array>
#include <optional>

namespace cubey::projects::sdf {
inline constexpr std::size_t authored_max_leaves = 1024;
inline constexpr std::size_t authored_max_instructions = 2048;
enum class ShapeCode : std::uint32_t {
    Box = 1,
    Sphere = 2,
    Capsule = 3,
    Cylinder = 4,
    Torus = 5,
    CappedCone = 6,
    RoundCone = 7,
    Union = 10,
    Subtract = 11,
    SmoothUnion = 12,
    SmoothSubtract = 13
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
struct alignas(16) MaterialPattern {
    math::Vec4 settings{0, 7, 6,
                        .5F}; // kind (0 plain, 1 stone, 2 marble), seed, scale, color strength
    math::Vec4 secondary{.2F, .18F, .15F, .08F}; // linear RGB, roughness variation
    math::Vec4 veins{1, .3F, .2F, 5};            // direction, frequency
    math::Vec4 detail{.0005F, 160, .10F, 5};     // bump height, grain scale, vein width, warp
    math::Vec4 offset{0}; // fixed model-space domain offset XYZ, marble body color variation
};
static_assert(sizeof(MaterialPattern) == 80);
struct AuthoredStep {
    std::vector<std::string> nodes;
    std::string label;
    double seconds = 0.65;
    std::vector<SceneInstruction> cutters;
};
struct AuthoredScene {
    std::string name;
    std::string document; // Explicit named tree; retained for faithful persistence.
    std::vector<SceneInstruction> program;
    std::vector<std::string> part_names; // One name per instruction, including groups.
    std::vector<std::string> material_names;
    std::vector<SurfaceMaterial> materials;
    std::vector<MaterialPattern> patterns;
    std::vector<AuthoredStep> history;
    std::size_t leaf_count = 0;
};
struct SurfaceSample {
    float distance = 1000;
    SurfaceMaterial material;
    std::uint32_t part = 0;
    math::Vec3 height_gradient{0};
};
[[nodiscard]] AuthoredScene parse_authored_scene(std::string_view text);
[[nodiscard]] AuthoredScene load_authored_scene(const std::filesystem::path& path);
[[nodiscard]] std::string serialize_authored_scene(const AuthoredScene& scene);
void save_authored_scene(const std::filesystem::path& path, const AuthoredScene& scene);
[[nodiscard]] std::uint64_t authored_geometry_hash(const AuthoredScene& scene);
[[nodiscard]] std::uint64_t authored_appearance_hash(const AuthoredScene& scene);
[[nodiscard]] float authored_distance(math::Vec3 point, const AuthoredScene& scene);
[[nodiscard]] SurfaceSample authored_surface(math::Vec3 point, const AuthoredScene& scene,
                                             float footprint = 0);
// Header followed by explicit vec4 instructions; suitable for a storage buffer.
[[nodiscard]] std::vector<std::uint8_t> authored_program_bytes(const AuthoredScene* scene);
} // namespace cubey::projects::sdf
