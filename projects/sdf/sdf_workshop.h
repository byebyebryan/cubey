#pragma once
#include "sdf_authored.h"
#include "sdf_config.h"
#include <cubey/asset/material_recipe.h>
namespace cubey::projects::sdf {
[[nodiscard]] AuthoredScene make_material_workshop(const SdfConfig& config);
[[nodiscard]] asset::MaterialRecipe workshop_material_recipe(const SdfConfig&,
                                                             const AuthoredScene&);
} // namespace cubey::projects::sdf
