#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace cubey::render {

struct AdaptivePatchLodPatchId {
    std::uint32_t root = 0;
    std::uint32_t level = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;

    friend bool operator==(const AdaptivePatchLodPatchId&,
                           const AdaptivePatchLodPatchId&) = default;
};

struct AdaptivePatchLodPatchInstance {
    AdaptivePatchLodPatchId id{};
    float screen_error_px = 0.0F;
};

struct AdaptivePatchLodConfig {
    std::uint32_t root_count = 1;
    std::uint32_t root_divisions_per_axis = 1;
    std::uint32_t max_lod_level = 0;
    std::uint32_t max_selected_patches = 1;
    float target_error_px = 1.0F;
    float hysteresis = 0.0F;
};

struct AdaptivePatchLodSelectionHints {
    std::span<const AdaptivePatchLodPatchId> previous_selected_patches{};
};

enum class AdaptivePatchLodCullResult : std::uint8_t {
    Visible,
    HorizonCulled,
    ViewCulled,
};

struct AdaptivePatchLodCallbacks {
    std::function<float(AdaptivePatchLodPatchId)> screen_error_px;
    std::function<AdaptivePatchLodCullResult(const AdaptivePatchLodPatchInstance&)>
        refinement_cull;
};

struct AdaptivePatchLodDiagnostics {
    std::uint32_t root_patch_count = 0;
    std::uint32_t planned_patch_count = 0;
    std::uint32_t visible_patch_count = 0;
    std::uint32_t culled_horizon_count = 0;
    std::uint32_t culled_view_count = 0;
    std::uint32_t patch_count = 0;
    std::uint32_t base_patch_count = 0;
    std::uint32_t refined_patch_count = 0;
    std::uint32_t subdivided_patch_count = 0;
    std::uint32_t refinement_fallback_patch_count = 0;
    std::uint32_t budget_fallback_patch_count = 0;
    std::uint32_t hysteresis_delayed_split_count = 0;
    std::uint32_t hysteresis_delayed_merge_count = 0;
    std::uint32_t transition_candidate_count = 0;
    std::uint32_t lod_neighbor_edge_count = 0;
    std::uint32_t lod_neighbor_boundary_edge_count = 0;
    std::uint32_t lod_neighbor_mismatch_edge_count = 0;
    std::uint32_t max_lod_neighbor_delta = 0;
    std::uint32_t lod_neighbor_repaired_split_count = 0;
    std::uint32_t min_lod_level = 0;
    std::uint32_t max_lod_level = 0;
    float min_screen_error_px = 0.0F;
    float max_screen_error_px = 0.0F;
    float max_transition_pressure = 0.0F;
    std::vector<std::uint32_t> patches_by_lod{};
};

struct AdaptivePatchLodNeighborDiagnostics {
    std::uint32_t edge_count = 0;
    std::uint32_t boundary_edge_count = 0;
    std::uint32_t mismatch_edge_count = 0;
    std::uint32_t max_lod_delta = 0;
};

struct AdaptivePatchLodPlan {
    std::vector<AdaptivePatchLodPatchInstance> selected_patches{};
    AdaptivePatchLodDiagnostics diagnostics{};
};

void validate_adaptive_patch_lod_config(const AdaptivePatchLodConfig& config);
[[nodiscard]] std::uint32_t
adaptive_patch_lod_root_patch_count(const AdaptivePatchLodConfig& config);
[[nodiscard]] std::uint32_t
adaptive_patch_lod_level_divisions(const AdaptivePatchLodConfig& config, std::uint32_t level);
[[nodiscard]] AdaptivePatchLodPatchId
adaptive_patch_lod_child_id(AdaptivePatchLodPatchId id, std::uint32_t child_index);
[[nodiscard]] AdaptivePatchLodPlan
plan_adaptive_patch_lod(const AdaptivePatchLodConfig& config,
                        const AdaptivePatchLodCallbacks& callbacks,
                        AdaptivePatchLodSelectionHints hints = {});
[[nodiscard]] AdaptivePatchLodNeighborDiagnostics
analyze_adaptive_patch_lod_neighbors(const AdaptivePatchLodConfig& config,
                                     std::span<const AdaptivePatchLodPatchInstance> patches);
[[nodiscard]] std::uint32_t
adaptive_patch_lod_edge_transition_mask(const AdaptivePatchLodConfig& config,
                                        std::span<const AdaptivePatchLodPatchInstance> patches,
                                        AdaptivePatchLodPatchId id);

} // namespace cubey::render
