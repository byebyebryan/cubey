#pragma once
#include "raytracing_config.h"
#include <cubey/asset/material_recipe.h>
#include <nlohmann/json.hpp>

namespace cubey::projects::raytracing {
struct Scene;
struct alignas(16) GpuRecipe {
    procedural::SurfaceMaterial surface;
    procedural::MaterialPattern pattern;
    procedural::MaterialOptics optics;
    math::Mat4 material_from_object{1};
    procedural::SurfaceLibrary library;
    glm::uvec4 routing{};
};
static_assert(sizeof(GpuRecipe) == 368);
static_assert(offsetof(GpuRecipe, pattern) == 32 && offsetof(GpuRecipe, optics) == 112);
static_assert(offsetof(GpuRecipe, material_from_object) == 144);
static_assert(offsetof(GpuRecipe, library) == 208 && offsetof(GpuRecipe, routing) == 352);
GpuRecipe gpu_recipe(const asset::MaterialRecipe&, std::uint32_t slot = 0);
asset::MaterialRecipe marble_base_recipe();
struct alignas(16) WorkshopUniform {
    // sampling.w: legacy recipe mask, or selected instance + 1 in dragon layout.
    glm::uvec4 options{}, sampling{};
    math::Vec4 solar{}; // angular radius, sky gain, solar gain, furnace
};
static_assert(sizeof(WorkshopUniform) == 48);

struct WorkshopState {
    std::vector<asset::MaterialCatalogEntry> catalog;
    std::vector<asset::MaterialRecipe> recipes;
    std::uint32_t selected = 0;
    std::uint64_t revision = 1;
    bool opaque_only = false;

    explicit WorkshopState(const Config&);
    void replace(std::size_t index, asset::MaterialRecipe candidate);
    void reload();
    [[nodiscard]] std::vector<GpuRecipe> gpu_records() const;
    [[nodiscard]] nlohmann::json identity() const;
    [[nodiscard]] bool supports(std::size_t index) const;
};
Scene make_material_scene(const Config&);
Scene make_material_dragon(const Config&);
} // namespace cubey::projects::raytracing
