#include <cubey/render/clipmap_grid_2d.h>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_throws(auto&& action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_clipmap_grid_2d_emits_far_to_near_annular_patches() {
    const cubey::render::ClipmapGrid2DConfig config{
        .lod_levels = 3,
        .cells_per_axis = 64,
        .outer_half_extent = 512.0F,
        .transition_cells = 8.0F,
        .max_transition_ratio = 0.25F,
    };

    require(cubey::render::clipmap_grid_2d_patch_count(config.lod_levels) == 9U,
            "clipmap grid patch count should be center plus four annular patches per parent LOD");
    require_near(cubey::render::clipmap_grid_2d_near_half_extent(config), 128.0F, 0.001F,
                 "clipmap near half extent should divide the outer extent by LOD scale");
    require_near(cubey::render::clipmap_grid_2d_level_half_extent(config, 2U), 512.0F, 0.001F,
                 "clipmap outer level should reach the configured half extent");
    require_near(cubey::render::clipmap_grid_2d_near_cell_size(config), 4.0F, 0.001F,
                 "clipmap near cell size should derive from near extent and cells");
    require_near(cubey::render::clipmap_grid_2d_level_cell_size(config, 2U), 16.0F, 0.001F,
                 "clipmap coarse cell size should scale by level");

    const cubey::render::ClipmapGrid2DPatchList<9U> patches =
        cubey::render::clipmap_grid_2d_patches<9U>(config);
    require(patches.count == 9U, "clipmap grid should emit the expected patches");
    require(patches.patches.front().level == 2U, "clipmap grid should start with far patches");
    require(patches.patches.back().level == 0U, "clipmap grid should finish with center patch");
    require_near(patches.patches.front().bounds.max_x, 512.0F, 0.001F,
                 "clipmap far patch should reach outer half extent");

    const cubey::render::ClipmapGrid2DPatch& center = patches.patches.back();
    require(center.cells_x == 64U && center.cells_z == 64U,
            "clipmap center patch should use the requested grid resolution");
    require_near(center.bounds.min_x, -128.0F, 0.001F,
                 "clipmap center patch should span the near half extent");
    require_near(center.bounds.max_z, 128.0F, 0.001F,
                 "clipmap center patch should span the near half extent");

    const float transition =
        cubey::render::clipmap_grid_2d_transition_width(16.0F, 256.0F, 8.0F, 0.25F);
    require_near(transition, 64.0F, 0.001F,
                 "clipmap transition should be bounded by the parent ring extent");
    std::uint32_t total_triangles = 0U;
    for (const cubey::render::ClipmapGrid2DPatch& patch : patches) {
        total_triangles += patch.cells_x * patch.cells_z * 2U;
    }
    require(total_triangles > center.cells_x * center.cells_z * 2U,
            "clipmap rings should add horizon geometry around the center patch");
}

void test_clipmap_grid_2d_rejects_invalid_config() {
    cubey::render::ClipmapGrid2DConfig config{};
    config.lod_levels = 0U;
    require_throws([&config] { (void)cubey::render::clipmap_grid_2d_patches<1U>(config); },
                   "clipmap grid should reject zero LOD levels");

    config = {};
    config.cells_per_axis = 0U;
    require_throws([&config] { (void)cubey::render::clipmap_grid_2d_patches<1U>(config); },
                   "clipmap grid should reject zero cell count");

    config = {.lod_levels = 2, .cells_per_axis = 8, .outer_half_extent = 10.0F};
    require_throws([&config] { (void)cubey::render::clipmap_grid_2d_patches<1U>(config); },
                   "clipmap grid should reject too-small patch storage");

    require_throws([] { (void)cubey::render::clipmap_grid_2d_cells_for_span(0.0F, 1.0F); },
                   "clipmap grid should reject non-positive spans");
    require_throws(
        [] { (void)cubey::render::clipmap_grid_2d_transition_width(1.0F, 0.0F, 1.0F, 0.35F); },
        "clipmap grid should reject non-positive transition bounds");
    require_throws(
        [] {
            cubey::render::ClipmapGrid2DConfig invalid{};
            invalid.outer_half_extent = std::numeric_limits<float>::quiet_NaN();
            (void)cubey::render::clipmap_grid_2d_patches<1U>(invalid);
        },
        "clipmap grid should reject NaN extents");
    require_throws(
        [] {
            (void)cubey::render::clipmap_grid_2d_transition_width(
                std::numeric_limits<float>::infinity(), 1.0F, 1.0F, 0.35F);
        },
        "clipmap grid should reject non-finite transition inputs");
    require_throws(
        [] {
            (void)cubey::render::clipmap_grid_2d_cells_for_span(
                std::numeric_limits<float>::infinity(), 1.0F);
        },
        "clipmap grid should reject non-finite patch spans");
    require_throws(
        [] {
            (void)cubey::render::clipmap_grid_2d_cells_for_span(std::numeric_limits<float>::max(),
                                                                0.5F);
        },
        "clipmap grid should reject patch cell count overflow");
}
