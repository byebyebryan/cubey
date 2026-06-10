#include <cubey/render/adaptive_patch_lod.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

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

[[nodiscard]] cubey::render::AdaptivePatchLodConfig small_config() {
    return {
        .root_count = 1,
        .root_divisions_per_axis = 1,
        .max_lod_level = 1,
        .max_selected_patches = 32,
        .target_error_px = 1.0F,
        .hysteresis = 0.0F,
    };
}

[[nodiscard]] cubey::render::AdaptivePatchLodCallbacks level_split_callbacks(float root_error,
                                                                             float child_error) {
    return {
        .screen_error_px =
            [root_error, child_error](cubey::render::AdaptivePatchLodPatchId id) {
                return id.level == 0U ? root_error : child_error;
            },
        .refinement_cull = {},
    };
}

} // namespace

void test_adaptive_patch_lod_selects_quadtree_children() {
    const cubey::render::AdaptivePatchLodConfig config = small_config();
    const cubey::render::AdaptivePatchLodPlan plan =
        cubey::render::plan_adaptive_patch_lod(config, level_split_callbacks(2.0F, 0.25F));

    require(plan.selected_patches.size() == 4U,
            "adaptive patch LOD should split an over-budget root into children");
    require(plan.diagnostics.root_patch_count == 1U,
            "adaptive patch LOD should report root patch count");
    require(plan.diagnostics.planned_patch_count == 5U,
            "adaptive patch LOD should count parent candidates and child leaves");
    require(plan.diagnostics.subdivided_patch_count == 1U,
            "adaptive patch LOD should report subdivided parents");
    require(plan.diagnostics.max_lod_level == 1U,
            "adaptive patch LOD should report selected child level");
    require(plan.diagnostics.patches_by_lod.size() == 2U &&
                plan.diagnostics.patches_by_lod[1] == 4U,
            "adaptive patch LOD should track selected patches by level");
}

void test_adaptive_patch_lod_hysteresis_delays_split_and_merge() {
    cubey::render::AdaptivePatchLodConfig config = small_config();
    config.hysteresis = 0.20F;

    const cubey::render::AdaptivePatchLodPatchId root{};
    const std::vector<cubey::render::AdaptivePatchLodPatchId> previous_root{root};
    const cubey::render::AdaptivePatchLodPlan delayed_split =
        cubey::render::plan_adaptive_patch_lod(
            config, level_split_callbacks(1.05F, 0.25F),
            cubey::render::AdaptivePatchLodSelectionHints{
                .previous_selected_patches = previous_root,
            });
    require(delayed_split.diagnostics.max_lod_level == 0U,
            "adaptive patch LOD should keep previous parent patches stable just above target");
    require(delayed_split.diagnostics.hysteresis_delayed_split_count > 0U,
            "adaptive patch LOD should record delayed splits");

    std::vector<cubey::render::AdaptivePatchLodPatchId> previous_children{};
    for (std::uint32_t child = 0; child < 4U; ++child) {
        previous_children.push_back(cubey::render::adaptive_patch_lod_child_id(root, child));
    }
    const cubey::render::AdaptivePatchLodPlan delayed_merge =
        cubey::render::plan_adaptive_patch_lod(
            config, level_split_callbacks(0.95F, 0.25F),
            cubey::render::AdaptivePatchLodSelectionHints{
                .previous_selected_patches = previous_children,
            });
    require(delayed_merge.diagnostics.max_lod_level == 1U,
            "adaptive patch LOD should keep previous child patches stable just below target");
    require(delayed_merge.diagnostics.hysteresis_delayed_merge_count > 0U,
            "adaptive patch LOD should record delayed merges");
}

void test_adaptive_patch_lod_falls_back_at_patch_budget() {
    const cubey::render::AdaptivePatchLodConfig config{
        .root_count = 1,
        .root_divisions_per_axis = 1,
        .max_lod_level = 5,
        .max_selected_patches = 12,
        .target_error_px = 0.5F,
    };
    const cubey::render::AdaptivePatchLodPlan plan =
        cubey::render::plan_adaptive_patch_lod(
            config, {
                        .screen_error_px =
                            [](cubey::render::AdaptivePatchLodPatchId) {
                                return 10.0F;
                            },
                        .refinement_cull = {},
                    });

    require(plan.selected_patches.size() <= config.max_selected_patches,
            "adaptive patch LOD should stay inside the live patch budget");
    require(plan.diagnostics.budget_fallback_patch_count > 0U,
            "adaptive patch LOD should record budget fallback");
}

void test_adaptive_patch_lod_repairs_neighbor_deltas() {
    const cubey::render::AdaptivePatchLodConfig config{
        .root_count = 1,
        .root_divisions_per_axis = 2,
        .max_lod_level = 4,
        .max_selected_patches = 256,
        .target_error_px = 1.0F,
    };
    const cubey::render::AdaptivePatchLodPlan plan =
        cubey::render::plan_adaptive_patch_lod(
            config, {
                        .screen_error_px =
                            [](cubey::render::AdaptivePatchLodPatchId id) {
                                const std::uint32_t right_edge_x =
                                    id.level == 0U ? 0U : (1U << id.level) - 1U;
                                if (id.level < 4U && id.x == right_edge_x && id.y == 0U) {
                                    return 10.0F;
                                }
                                return 0.1F;
                            },
                        .refinement_cull = {},
                    });

    require(plan.diagnostics.max_lod_neighbor_delta <= 1U,
            "adaptive patch LOD should repair selected neighbor deltas to one step");
    require(plan.diagnostics.lod_neighbor_repaired_split_count > 0U,
            "adaptive patch LOD should report neighbor repair splits");
}

void test_adaptive_patch_lod_marks_edges_against_coarser_neighbors() {
    const cubey::render::AdaptivePatchLodConfig config{
        .root_count = 1,
        .root_divisions_per_axis = 2,
        .max_lod_level = 1,
        .max_selected_patches = 16,
    };
    const std::vector<cubey::render::AdaptivePatchLodPatchInstance> patches{
        {.id = {.root = 0, .level = 1, .x = 0, .y = 0}},
        {.id = {.root = 0, .level = 1, .x = 1, .y = 0}},
        {.id = {.root = 0, .level = 1, .x = 0, .y = 1}},
        {.id = {.root = 0, .level = 1, .x = 1, .y = 1}},
        {.id = {.root = 0, .level = 0, .x = 1, .y = 0}},
    };

    bool found_transition = false;
    for (const cubey::render::AdaptivePatchLodPatchInstance& patch : patches) {
        const std::uint32_t mask =
            cubey::render::adaptive_patch_lod_edge_transition_mask(config, patches, patch.id);
        if (patch.id.level == 1U && mask != 0U) {
            found_transition = true;
        }
        if (patch.id.level == 0U) {
            require(mask == 0U,
                    "adaptive patch LOD should not mark coarse edges against finer neighbors");
        }
    }
    require(found_transition,
            "adaptive patch LOD should mark fine edges against coarser neighbors");
}

void test_adaptive_patch_lod_rejects_invalid_config_and_callbacks() {
    cubey::render::AdaptivePatchLodConfig config{};
    config.root_count = 0U;
    require_throws([&config] { cubey::render::validate_adaptive_patch_lod_config(config); },
                   "adaptive patch LOD should reject zero roots");

    config = small_config();
    config.max_selected_patches = 0U;
    require_throws([&config] { cubey::render::validate_adaptive_patch_lod_config(config); },
                   "adaptive patch LOD should reject root coverage larger than budget");

    config = small_config();
    config.hysteresis = 1.0F;
    require_throws([&config] { cubey::render::validate_adaptive_patch_lod_config(config); },
                   "adaptive patch LOD should reject invalid hysteresis");

    config = small_config();
    require_throws(
        [&config] {
            (void)cubey::render::plan_adaptive_patch_lod(config, {});
        },
        "adaptive patch LOD should require a screen-error callback");

    require_throws(
        [&config] {
            (void)cubey::render::plan_adaptive_patch_lod(
                config, {
                            .screen_error_px =
                                [](cubey::render::AdaptivePatchLodPatchId) {
                                    return std::numeric_limits<float>::quiet_NaN();
                                },
                            .refinement_cull = {},
                        });
        },
        "adaptive patch LOD should reject non-finite screen error");
}
