#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace cubey::projects::fluid::fluid_25d {

// Scenic-only artistic controls. Never used by hydraulic or coverage code.
struct Fluid25DScenicMaterial {
    float wet_roughness = 0.38F;
    float wet_darkening = 0.72F;
    float terrain_saturation = 1.0F;
    float terrain_ambient = 1.0F;
    float terrain_direct = 1.0F;
    float shadow_strength = 1.0F;
    float water_roughness = 0.18F;
    float water_normal_strength = 1.0F;
    float water_extinction_scale = 1.0F;
    float water_scatter_scale = 1.0F;
    float water_reflection_scale = 1.0F;
    float water_scatter_lighting = 0.0F;
    float water_clarity = 0.0F;
    float terrain_mineral_scale = 1.0F;
    float terrain_material_blend = 0.0F;
    // -1 retains the legacy RG-to-world-XZ shortcut; >=0 uses surface gradients.
    float terrain_normal_strength = -1.0F;
    float terrain_ambient_softening = 0.0F;
    float terrain_diffuse_convolution = 0.0F;
    float terrain_specular_scale = 1.0F;
    float terrain_shadow_scale = 1.0F;
    float terrain_slope_color_scale = 1.0F;
};

[[nodiscard]] Fluid25DScenicMaterial fluid_25d_scenic_material(std::string_view profile);
[[nodiscard]] Fluid25DScenicMaterial fluid_25d_parse_scenic_material(std::string_view text,
                                                                     Fluid25DScenicMaterial base);
[[nodiscard]] Fluid25DScenicMaterial
fluid_25d_load_scenic_material(const std::filesystem::path& path, Fluid25DScenicMaterial base);
[[nodiscard]] std::string fluid_25d_scenic_material_json(const Fluid25DScenicMaterial& material);

// Nonzero views suppress water/dot draws, never native field uploads.
// Component colors retain Scenic exposure and tonemapping.
[[nodiscard]] unsigned fluid_25d_scenic_terrain_view(std::string_view view);

} // namespace cubey::projects::fluid::fluid_25d
