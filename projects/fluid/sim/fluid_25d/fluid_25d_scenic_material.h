#pragma once

#include <cubey/render/terrain_surface_patch_material.h>
#include <filesystem>
#include <string>
#include <string_view>

namespace cubey::projects::fluid::fluid_25d {
using Fluid25DScenicMaterial = render::TerrainSurfacePatchMaterial;

[[nodiscard]] Fluid25DScenicMaterial fluid_25d_scenic_material(std::string_view profile);
[[nodiscard]] Fluid25DScenicMaterial fluid_25d_parse_scenic_material(std::string_view text,
                                                                     Fluid25DScenicMaterial base);
[[nodiscard]] Fluid25DScenicMaterial
fluid_25d_load_scenic_material(const std::filesystem::path& path, Fluid25DScenicMaterial base);
[[nodiscard]] std::string fluid_25d_scenic_material_json(const Fluid25DScenicMaterial& material);
[[nodiscard]] unsigned fluid_25d_scenic_terrain_view(std::string_view view);
[[nodiscard]] unsigned fluid_25d_scenic_water_view(std::string_view view);
} // namespace cubey::projects::fluid::fluid_25d
