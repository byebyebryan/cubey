#pragma once
#include <cubey/asset/material_recipe.h>
#include <cubey/render/pbr.h>
#include <cmath>
#include <stdexcept>
namespace cubey {
// Thickness is a specimen estimate in object geometry units, independent of
// material coordinates. Mesh transport uses the existing forward volume path.
[[nodiscard]] inline render::PbrMaterialDefinition pbr_material_from_recipe(
    const asset::MaterialRecipe& r, float thickness, std::uint32_t sort_key = 0) {
    asset::validate_material_recipe(r);
    if (!std::isfinite(thickness) || thickness < 0) throw std::invalid_argument("invalid mesh thickness");
    render::PbrMaterialDefinition d;
    d.label = r.label;
    d.sort_key = sort_key;
    d.cull_mode = VK_CULL_MODE_NONE;
    auto& f=d.factors;
    f.base_color_factor={math::Vec3{r.surface.base_roughness},1};
    f.roughness_factor=r.surface.base_roughness.w;
    f.emissive_factor=math::Vec3{r.surface.emission};
    f.metallic_factor=r.optics.settings.x;
    f.transmission_factor=r.optics.settings.y;
    f.dielectric_ior=r.optics.settings.z;
    f.volume_thickness_factor=thickness;
    f.volume_attenuation_distance=r.optics.settings.w;
    f.volume_attenuation_color=math::Vec3{r.optics.attenuation};
    f.diffuse_roughness=r.optics.attenuation.w;
    f.surface_pattern=r.pattern;
    f.material_from_object=r.material_from_object;
    return d;
}
}
