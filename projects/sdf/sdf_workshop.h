#pragma once
#include "sdf_authored.h"
#include "sdf_config.h"
#include <cubey/asset/material_recipe.h>
namespace cubey::projects::sdf {
// Geometry/CSG changes retain the working materials. Pass null for an explicit
// recipe selection, reload or reset.
[[nodiscard]] AuthoredScene make_material_workshop(const SdfConfig& config,
                                                   const AuthoredScene* working = nullptr);
[[nodiscard]] asset::MaterialRecipe workshop_material_recipe(const SdfConfig&,
                                                             const AuthoredScene&);
} // namespace cubey::projects::sdf
