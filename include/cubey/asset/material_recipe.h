#pragma once
#include <cubey/procedural/surface_material.h>
#include <filesystem>
#include <string>
#include <string_view>

namespace cubey::asset {
// Linear colors; material_from_object maps object positions into explicit
// material units. Attenuation distance is independently in geometry units.
struct MaterialRecipe {
    std::string id, label;
    procedural::SurfaceMaterial surface;
    procedural::MaterialPattern pattern;
    procedural::MaterialOptics optics;
    math::Mat4 material_from_object{1};
};
void validate_material_recipe(const MaterialRecipe& recipe);
[[nodiscard]] MaterialRecipe parse_material_recipe(std::string_view text);
[[nodiscard]] MaterialRecipe load_material_recipe(const std::filesystem::path& path);
[[nodiscard]] std::string serialize_material_recipe(const MaterialRecipe& recipe);
void save_material_recipe(const std::filesystem::path& path, const MaterialRecipe& recipe);
[[nodiscard]] std::uint64_t material_recipe_fingerprint(const MaterialRecipe& recipe);
} // namespace cubey::asset
