#include <cubey/render/adaptive_patch_lod.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_set>

namespace cubey::render {
namespace {

struct AdaptivePatchLodPatchIdHash {
    [[nodiscard]] std::size_t operator()(AdaptivePatchLodPatchId id) const noexcept {
        std::uint64_t hash = id.root * 73856093ULL;
        hash ^= id.level * 19349663ULL;
        hash ^= id.x * 83492791ULL;
        hash ^= id.y * 2654435761ULL;
        return static_cast<std::size_t>(hash);
    }
};

[[nodiscard]] AdaptivePatchLodPatchId parent_patch_id(AdaptivePatchLodPatchId id) {
    return {
        .root = id.root,
        .level = id.level - 1U,
        .x = id.x >> 1U,
        .y = id.y >> 1U,
    };
}

class AdaptivePatchLodSelectionLookup {
  public:
    explicit AdaptivePatchLodSelectionLookup(std::span<const AdaptivePatchLodPatchId> previous) {
        selected_.reserve(previous.size());
        refined_ancestors_.reserve(previous.size());
        for (AdaptivePatchLodPatchId id : previous) {
            selected_.insert(id);
            while (id.level > 0U) {
                id = parent_patch_id(id);
                refined_ancestors_.insert(id);
            }
        }
    }

    [[nodiscard]] bool was_selected(AdaptivePatchLodPatchId id) const {
        return selected_.contains(id);
    }

    [[nodiscard]] bool was_refined(AdaptivePatchLodPatchId id) const {
        return refined_ancestors_.contains(id);
    }

  private:
    std::unordered_set<AdaptivePatchLodPatchId, AdaptivePatchLodPatchIdHash> selected_;
    std::unordered_set<AdaptivePatchLodPatchId, AdaptivePatchLodPatchIdHash> refined_ancestors_;
};

class AdaptivePatchLodSelectionSet {
  public:
    explicit AdaptivePatchLodSelectionSet(std::span<const AdaptivePatchLodPatchInstance> patches) {
        selected_.reserve(patches.size());
        for (const AdaptivePatchLodPatchInstance& patch : patches) {
            selected_.insert(patch.id);
        }
    }

    [[nodiscard]] bool contains(AdaptivePatchLodPatchId id) const {
        return selected_.contains(id);
    }

  private:
    std::unordered_set<AdaptivePatchLodPatchId, AdaptivePatchLodPatchIdHash> selected_;
};

[[nodiscard]] float lod_transition_pressure(float error_px, float target_px) {
    const float ratio = error_px / std::max(target_px, 0.0001F);
    return 1.0F - std::clamp(std::abs(ratio - 1.0F) / 0.25F, 0.0F, 1.0F);
}

[[nodiscard]] std::uint32_t max_lod_grid_divisions(const AdaptivePatchLodConfig& config) {
    return adaptive_patch_lod_level_divisions(config, config.max_lod_level);
}

struct PatchGridSpan {
    std::uint32_t x0 = 0;
    std::uint32_t y0 = 0;
    std::uint32_t x1 = 0;
    std::uint32_t y1 = 0;
};

[[nodiscard]] PatchGridSpan patch_grid_span(const AdaptivePatchLodConfig& config,
                                            AdaptivePatchLodPatchId id) {
    const std::uint32_t total_divisions = max_lod_grid_divisions(config);
    const std::uint32_t patch_divisions =
        adaptive_patch_lod_level_divisions(config, id.level);
    const std::uint32_t scale = total_divisions / patch_divisions;
    return {
        .x0 = id.x * scale,
        .y0 = id.y * scale,
        .x1 = (id.x + 1U) * scale,
        .y1 = (id.y + 1U) * scale,
    };
}

[[nodiscard]] std::optional<AdaptivePatchLodPatchId>
find_selected_patch_covering_cell(const AdaptivePatchLodConfig& config,
                                  const AdaptivePatchLodSelectionSet& set,
                                  std::uint32_t root,
                                  std::uint32_t cell_x,
                                  std::uint32_t cell_y) {
    const std::uint32_t total_divisions = max_lod_grid_divisions(config);
    for (int level = static_cast<int>(config.max_lod_level); level >= 0; --level) {
        const auto level_u32 = static_cast<std::uint32_t>(level);
        const std::uint32_t divisions = adaptive_patch_lod_level_divisions(config, level_u32);
        const AdaptivePatchLodPatchId id{
            .root = root,
            .level = level_u32,
            .x = std::min((cell_x * divisions) / total_divisions, divisions - 1U),
            .y = std::min((cell_y * divisions) / total_divisions, divisions - 1U),
        };
        if (set.contains(id)) {
            return id;
        }
    }
    return std::nullopt;
}

struct NeighborEdgeProbe {
    bool boundary = false;
    bool found_neighbor = false;
    std::uint32_t max_delta = 0;
    std::uint32_t max_coarser_delta = 0;
    std::uint32_t max_finer_delta = 0;
};

[[nodiscard]] NeighborEdgeProbe analyze_neighbor_edge(const AdaptivePatchLodConfig& config,
                                                      const AdaptivePatchLodSelectionSet& set,
                                                      AdaptivePatchLodPatchId id,
                                                      std::uint32_t edge_index) {
    const std::uint32_t total_divisions = max_lod_grid_divisions(config);
    const PatchGridSpan span = patch_grid_span(config, id);
    const bool left = edge_index == 0U;
    const bool right = edge_index == 1U;
    const bool bottom = edge_index == 2U;
    const bool top = edge_index == 3U;
    if ((left && span.x0 == 0U) || (right && span.x1 >= total_divisions) ||
        (bottom && span.y0 == 0U) || (top && span.y1 >= total_divisions)) {
        return {.boundary = true};
    }

    NeighborEdgeProbe probe{};
    const std::uint32_t edge_length = (left || right) ? (span.y1 - span.y0) : (span.x1 - span.x0);
    for (std::uint32_t sample = 1U; sample <= 3U; ++sample) {
        const std::uint32_t along =
            ((left || right) ? span.y0 : span.x0) + (edge_length * sample) / 4U;
        const std::uint32_t cell_x =
            left ? span.x0 - 1U : right ? span.x1 : std::min(along, total_divisions - 1U);
        const std::uint32_t cell_y =
            bottom ? span.y0 - 1U : top ? span.y1 : std::min(along, total_divisions - 1U);
        const std::optional<AdaptivePatchLodPatchId> neighbor =
            find_selected_patch_covering_cell(config, set, id.root, cell_x, cell_y);
        if (!neighbor.has_value()) {
            continue;
        }
        probe.found_neighbor = true;
        const std::uint32_t delta = id.level > neighbor->level ? id.level - neighbor->level
                                                               : neighbor->level - id.level;
        probe.max_delta = std::max(probe.max_delta, delta);
        if (id.level > neighbor->level) {
            probe.max_coarser_delta = std::max(probe.max_coarser_delta, id.level - neighbor->level);
        } else if (neighbor->level > id.level) {
            probe.max_finer_delta = std::max(probe.max_finer_delta, neighbor->level - id.level);
        }
    }
    return probe;
}

void update_screen_error_range(AdaptivePatchLodDiagnostics& diagnostics, float value) {
    if (diagnostics.visible_patch_count == 0U || diagnostics.min_screen_error_px == 0.0F) {
        diagnostics.min_screen_error_px = value;
    } else {
        diagnostics.min_screen_error_px = std::min(diagnostics.min_screen_error_px, value);
    }
    diagnostics.max_screen_error_px = std::max(diagnostics.max_screen_error_px, value);
}

void update_lod_transition_diagnostics(float target_error_px,
                                       AdaptivePatchLodDiagnostics& diagnostics,
                                       float error_px) {
    const float pressure = lod_transition_pressure(error_px, target_error_px);
    diagnostics.max_transition_pressure = std::max(diagnostics.max_transition_pressure, pressure);
    if (pressure > 0.0F) {
        ++diagnostics.transition_candidate_count;
    }
}

void reset_selected_patch_diagnostics(AdaptivePatchLodDiagnostics& diagnostics) {
    diagnostics.visible_patch_count = 0;
    diagnostics.patch_count = 0;
    diagnostics.base_patch_count = 0;
    diagnostics.refined_patch_count = 0;
    diagnostics.transition_candidate_count = 0;
    diagnostics.min_lod_level = 0;
    diagnostics.max_lod_level = 0;
    diagnostics.min_screen_error_px = 0.0F;
    diagnostics.max_screen_error_px = 0.0F;
    diagnostics.max_transition_pressure = 0.0F;
    std::fill(diagnostics.patches_by_lod.begin(), diagnostics.patches_by_lod.end(), 0U);
}

void record_selected_patch_diagnostics(const AdaptivePatchLodConfig& config,
                                       AdaptivePatchLodDiagnostics& diagnostics,
                                       const AdaptivePatchLodPatchInstance& patch) {
    diagnostics.min_lod_level = diagnostics.visible_patch_count == 0U
                                    ? patch.id.level
                                    : std::min(diagnostics.min_lod_level, patch.id.level);
    diagnostics.max_lod_level = std::max(diagnostics.max_lod_level, patch.id.level);
    if (patch.id.level < diagnostics.patches_by_lod.size()) {
        ++diagnostics.patches_by_lod[patch.id.level];
    }
    update_screen_error_range(diagnostics, patch.screen_error_px);
    update_lod_transition_diagnostics(config.target_error_px, diagnostics, patch.screen_error_px);
    if (patch.id.level == 0U) {
        ++diagnostics.base_patch_count;
    } else {
        ++diagnostics.refined_patch_count;
    }
    ++diagnostics.visible_patch_count;
    diagnostics.patch_count = diagnostics.visible_patch_count;
}

void refresh_selected_patch_diagnostics(const AdaptivePatchLodConfig& config,
                                        AdaptivePatchLodPlan& plan) {
    reset_selected_patch_diagnostics(plan.diagnostics);
    for (const AdaptivePatchLodPatchInstance& patch : plan.selected_patches) {
        record_selected_patch_diagnostics(config, plan.diagnostics, patch);
    }
}

void record_refinement_cull(AdaptivePatchLodPlan& plan, AdaptivePatchLodCullResult cull) {
    if (cull == AdaptivePatchLodCullResult::HorizonCulled) {
        ++plan.diagnostics.culled_horizon_count;
    } else if (cull == AdaptivePatchLodCullResult::ViewCulled) {
        ++plan.diagnostics.culled_view_count;
    }
}

[[nodiscard]] float lod_refinement_threshold_px(const AdaptivePatchLodConfig& config,
                                                const AdaptivePatchLodSelectionLookup& lookup,
                                                AdaptivePatchLodPatchId id) {
    if (lookup.was_refined(id)) {
        return config.target_error_px * (1.0F - config.hysteresis);
    }
    if (lookup.was_selected(id)) {
        return config.target_error_px * (1.0F + config.hysteresis);
    }
    return config.target_error_px;
}

[[nodiscard]] bool can_refine_with_live_budget(const AdaptivePatchLodConfig& config,
                                               const AdaptivePatchLodPlan& plan) {
    const std::uint64_t fallback_depth_reserve =
        4ULL * (static_cast<std::uint64_t>(config.max_lod_level) + 1ULL);
    return static_cast<std::uint64_t>(plan.selected_patches.size()) +
               adaptive_patch_lod_root_patch_count(config) + fallback_depth_reserve + 4ULL <
           config.max_selected_patches;
}

[[nodiscard]] bool record_visible_patch(const AdaptivePatchLodConfig& config,
                                        AdaptivePatchLodPlan& plan,
                                        const AdaptivePatchLodPatchInstance& patch) {
    record_selected_patch_diagnostics(config, plan.diagnostics, patch);
    plan.selected_patches.push_back(patch);
    return plan.selected_patches.size() <= config.max_selected_patches;
}

[[nodiscard]] AdaptivePatchLodCullResult
refinement_cull_result(const AdaptivePatchLodCallbacks& callbacks,
                       const AdaptivePatchLodPatchInstance& patch) {
    if (!callbacks.refinement_cull) {
        return AdaptivePatchLodCullResult::Visible;
    }
    return callbacks.refinement_cull(patch);
}

[[nodiscard]] bool append_coverage_patches(const AdaptivePatchLodConfig& config,
                                           const AdaptivePatchLodCallbacks& callbacks,
                                           const AdaptivePatchLodSelectionLookup& lookup,
                                           AdaptivePatchLodPatchInstance patch,
                                           AdaptivePatchLodPlan& plan) {
    patch.screen_error_px = callbacks.screen_error_px(patch.id);
    if (!std::isfinite(patch.screen_error_px) || patch.screen_error_px < 0.0F) {
        throw std::runtime_error("adaptive patch LOD screen error must be finite and non-negative");
    }
    ++plan.diagnostics.planned_patch_count;

    const bool raw_wants_refinement =
        patch.id.level < config.max_lod_level && patch.screen_error_px > config.target_error_px;
    const float refinement_threshold_px = lod_refinement_threshold_px(config, lookup, patch.id);
    const bool wants_refinement =
        patch.id.level < config.max_lod_level && patch.screen_error_px > refinement_threshold_px;
    if (raw_wants_refinement && !wants_refinement) {
        ++plan.diagnostics.hysteresis_delayed_split_count;
    } else if (!raw_wants_refinement && wants_refinement) {
        ++plan.diagnostics.hysteresis_delayed_merge_count;
    }

    if (wants_refinement) {
        const AdaptivePatchLodCullResult cull = refinement_cull_result(callbacks, patch);
        if (cull != AdaptivePatchLodCullResult::Visible) {
            record_refinement_cull(plan, cull);
            ++plan.diagnostics.refinement_fallback_patch_count;
            return record_visible_patch(config, plan, patch);
        }
    }
    if (wants_refinement && !can_refine_with_live_budget(config, plan)) {
        ++plan.diagnostics.budget_fallback_patch_count;
        return record_visible_patch(config, plan, patch);
    }

    if (wants_refinement) {
        const std::size_t selected_patch_snapshot = plan.selected_patches.size();
        const AdaptivePatchLodDiagnostics diagnostics_snapshot = plan.diagnostics;
        ++plan.diagnostics.subdivided_patch_count;
        const bool children_fit =
            append_coverage_patches(
                config, callbacks, lookup,
                AdaptivePatchLodPatchInstance{
                    .id = adaptive_patch_lod_child_id(patch.id, 0U),
                },
                plan) &&
            append_coverage_patches(
                config, callbacks, lookup,
                AdaptivePatchLodPatchInstance{
                    .id = adaptive_patch_lod_child_id(patch.id, 1U),
                },
                plan) &&
            append_coverage_patches(
                config, callbacks, lookup,
                AdaptivePatchLodPatchInstance{
                    .id = adaptive_patch_lod_child_id(patch.id, 2U),
                },
                plan) &&
            append_coverage_patches(
                config, callbacks, lookup,
                AdaptivePatchLodPatchInstance{
                    .id = adaptive_patch_lod_child_id(patch.id, 3U),
                },
                plan);
        if (!children_fit) {
            plan.selected_patches.resize(selected_patch_snapshot);
            plan.diagnostics = diagnostics_snapshot;
            ++plan.diagnostics.budget_fallback_patch_count;
            return record_visible_patch(config, plan, patch);
        }
        return true;
    }
    return record_visible_patch(config, plan, patch);
}

[[nodiscard]] AdaptivePatchLodPlan make_adaptive_patch_lod_plan(
    const AdaptivePatchLodConfig& config,
    const AdaptivePatchLodCallbacks& callbacks,
    AdaptivePatchLodSelectionHints hints) {
    AdaptivePatchLodPlan plan{};
    plan.diagnostics.root_patch_count = adaptive_patch_lod_root_patch_count(config);
    plan.diagnostics.patches_by_lod.assign(config.max_lod_level + 1U, 0U);
    plan.selected_patches.reserve(
        std::min<std::uint32_t>(config.max_selected_patches, plan.diagnostics.root_patch_count));
    const AdaptivePatchLodSelectionLookup lookup{hints.previous_selected_patches};
    for (std::uint32_t root = 0; root < config.root_count; ++root) {
        for (std::uint32_t y = 0; y < config.root_divisions_per_axis; ++y) {
            for (std::uint32_t x = 0; x < config.root_divisions_per_axis; ++x) {
                if (!append_coverage_patches(config, callbacks, lookup,
                                             AdaptivePatchLodPatchInstance{
                                                 .id =
                                                     {
                                                         .root = root,
                                                         .level = 0,
                                                         .x = x,
                                                         .y = y,
                                                     },
                                             },
                                             plan)) {
                    throw std::runtime_error(
                        "adaptive patch LOD selection exceeded the root patch budget");
                }
            }
        }
    }
    return plan;
}

void update_neighbor_lod_diagnostics(AdaptivePatchLodDiagnostics& diagnostics,
                                     AdaptivePatchLodNeighborDiagnostics neighbor_diagnostics) {
    diagnostics.lod_neighbor_edge_count = neighbor_diagnostics.edge_count;
    diagnostics.lod_neighbor_boundary_edge_count = neighbor_diagnostics.boundary_edge_count;
    diagnostics.lod_neighbor_mismatch_edge_count = neighbor_diagnostics.mismatch_edge_count;
    diagnostics.max_lod_neighbor_delta = neighbor_diagnostics.max_lod_delta;
}

[[nodiscard]] AdaptivePatchLodPatchInstance patch_instance_for_id(
    const AdaptivePatchLodCallbacks& callbacks,
    AdaptivePatchLodPatchId id) {
    AdaptivePatchLodPatchInstance patch{.id = id};
    patch.screen_error_px = callbacks.screen_error_px(id);
    if (!std::isfinite(patch.screen_error_px) || patch.screen_error_px < 0.0F) {
        throw std::runtime_error("adaptive patch LOD screen error must be finite and non-negative");
    }
    return patch;
}

void replace_patch_with_children(const AdaptivePatchLodCallbacks& callbacks,
                                 AdaptivePatchLodPlan& plan,
                                 std::size_t patch_index) {
    const AdaptivePatchLodPatchId parent = plan.selected_patches.at(patch_index).id;
    std::array<AdaptivePatchLodPatchInstance, 4> children{};
    for (std::uint32_t child = 0; child < children.size(); ++child) {
        children[child] = patch_instance_for_id(callbacks, adaptive_patch_lod_child_id(parent, child));
    }
    plan.selected_patches[patch_index] = children[0];
    plan.selected_patches.insert(plan.selected_patches.begin() + static_cast<std::ptrdiff_t>(patch_index) +
                                     1,
                                 children.begin() + 1, children.end());
    ++plan.diagnostics.lod_neighbor_repaired_split_count;
}

enum class NeighborRepairStep : std::uint8_t {
    Done,
    Split,
    NeedsBudget,
};

[[nodiscard]] NeighborRepairStep repair_neighbor_lod_once(const AdaptivePatchLodConfig& config,
                                                          const AdaptivePatchLodCallbacks& callbacks,
                                                          AdaptivePatchLodPlan& plan) {
    const AdaptivePatchLodSelectionSet set{plan.selected_patches};
    for (std::size_t index = 0; index < plan.selected_patches.size(); ++index) {
        const AdaptivePatchLodPatchId id = plan.selected_patches[index].id;
        if (id.level >= config.max_lod_level) {
            continue;
        }
        for (std::uint32_t edge = 0; edge < 4U; ++edge) {
            const NeighborEdgeProbe probe = analyze_neighbor_edge(config, set, id, edge);
            if (probe.max_finer_delta <= 1U) {
                continue;
            }
            if (plan.selected_patches.size() + 3U > config.max_selected_patches) {
                return NeighborRepairStep::NeedsBudget;
            }
            replace_patch_with_children(callbacks, plan, index);
            return NeighborRepairStep::Split;
        }
    }
    return NeighborRepairStep::Done;
}

[[nodiscard]] bool coarsen_selected_patches_once(const AdaptivePatchLodConfig& config,
                                                 const AdaptivePatchLodCallbacks& callbacks,
                                                 AdaptivePatchLodPlan& plan) {
    std::vector<AdaptivePatchLodPatchInstance> coarsened;
    coarsened.reserve(plan.selected_patches.size());
    std::unordered_set<AdaptivePatchLodPatchId, AdaptivePatchLodPatchIdHash> seen;
    seen.reserve(plan.selected_patches.size());
    bool changed = false;
    for (const AdaptivePatchLodPatchInstance& patch : plan.selected_patches) {
        AdaptivePatchLodPatchId id = patch.id;
        if (id.level > 0U) {
            id = parent_patch_id(id);
            changed = true;
        }
        if (seen.insert(id).second) {
            coarsened.push_back(patch_instance_for_id(callbacks, id));
        }
    }
    if (!changed) {
        return false;
    }
    if (coarsened.size() > config.max_selected_patches) {
        return false;
    }
    plan.selected_patches = std::move(coarsened);
    ++plan.diagnostics.budget_fallback_patch_count;
    return true;
}

void repair_neighbor_lod_transitions(const AdaptivePatchLodConfig& config,
                                     const AdaptivePatchLodCallbacks& callbacks,
                                     AdaptivePatchLodPlan& plan) {
    const std::size_t iteration_limit =
        std::max<std::size_t>(1U, plan.selected_patches.size() * (config.max_lod_level + 1U));
    for (std::size_t iteration = 0; iteration < iteration_limit; ++iteration) {
        const NeighborRepairStep step = repair_neighbor_lod_once(config, callbacks, plan);
        if (step == NeighborRepairStep::Done) {
            refresh_selected_patch_diagnostics(config, plan);
            update_neighbor_lod_diagnostics(
                plan.diagnostics,
                analyze_adaptive_patch_lod_neighbors(config, plan.selected_patches));
            return;
        }
        if (step == NeighborRepairStep::NeedsBudget &&
            !coarsen_selected_patches_once(config, callbacks, plan)) {
            break;
        }
    }

    refresh_selected_patch_diagnostics(config, plan);
    update_neighbor_lod_diagnostics(
        plan.diagnostics, analyze_adaptive_patch_lod_neighbors(config, plan.selected_patches));
    if (plan.diagnostics.max_lod_neighbor_delta > 1U) {
        throw std::runtime_error("adaptive patch LOD neighbor repair could not enforce single-step deltas");
    }
}

} // namespace

void validate_adaptive_patch_lod_config(const AdaptivePatchLodConfig& config) {
    if (config.root_count == 0U) {
        throw std::runtime_error("adaptive patch LOD root count must be positive");
    }
    if (config.root_divisions_per_axis == 0U) {
        throw std::runtime_error("adaptive patch LOD root divisions must be positive");
    }
    if (config.max_lod_level >= 30U) {
        throw std::runtime_error("adaptive patch LOD max level out of supported range");
    }
    if (config.root_divisions_per_axis >
        (std::numeric_limits<std::uint32_t>::max() >> config.max_lod_level)) {
        throw std::runtime_error("adaptive patch LOD grid divisions exceed uint32 range");
    }
    const std::uint64_t root_patch_count =
        static_cast<std::uint64_t>(config.root_count) *
        static_cast<std::uint64_t>(config.root_divisions_per_axis) *
        static_cast<std::uint64_t>(config.root_divisions_per_axis);
    if (root_patch_count > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::runtime_error("adaptive patch LOD root patch count exceeds uint32 range");
    }
    if (root_patch_count > config.max_selected_patches) {
        throw std::runtime_error("adaptive patch LOD max selected patches cannot cover roots");
    }
    if (!std::isfinite(config.target_error_px) || config.target_error_px <= 0.0F) {
        throw std::runtime_error("adaptive patch LOD target error must be positive");
    }
    if (!std::isfinite(config.hysteresis) || config.hysteresis < 0.0F ||
        config.hysteresis >= 1.0F) {
        throw std::runtime_error("adaptive patch LOD hysteresis must be in [0, 1)");
    }
}

std::uint32_t adaptive_patch_lod_root_patch_count(const AdaptivePatchLodConfig& config) {
    validate_adaptive_patch_lod_config(config);
    return config.root_count * config.root_divisions_per_axis * config.root_divisions_per_axis;
}

std::uint32_t adaptive_patch_lod_level_divisions(const AdaptivePatchLodConfig& config,
                                                std::uint32_t level) {
    validate_adaptive_patch_lod_config(config);
    if (level > config.max_lod_level) {
        throw std::runtime_error("adaptive patch LOD level out of range");
    }
    return config.root_divisions_per_axis << level;
}

AdaptivePatchLodPatchId adaptive_patch_lod_child_id(AdaptivePatchLodPatchId id,
                                                    std::uint32_t child_index) {
    if (child_index >= 4U) {
        throw std::runtime_error("adaptive patch LOD child index must be < 4");
    }
    return {
        .root = id.root,
        .level = id.level + 1U,
        .x = id.x * 2U + (child_index & 1U),
        .y = id.y * 2U + (child_index >> 1U),
    };
}

AdaptivePatchLodPlan plan_adaptive_patch_lod(const AdaptivePatchLodConfig& config,
                                             const AdaptivePatchLodCallbacks& callbacks,
                                             AdaptivePatchLodSelectionHints hints) {
    validate_adaptive_patch_lod_config(config);
    if (!callbacks.screen_error_px) {
        throw std::runtime_error("adaptive patch LOD requires a screen-error callback");
    }
    AdaptivePatchLodPlan plan = make_adaptive_patch_lod_plan(config, callbacks, hints);
    if (plan.selected_patches.size() > config.max_selected_patches) {
        throw std::runtime_error("adaptive patch LOD fallback exceeded the patch instance budget");
    }
    repair_neighbor_lod_transitions(config, callbacks, plan);
    return plan;
}

AdaptivePatchLodNeighborDiagnostics
analyze_adaptive_patch_lod_neighbors(const AdaptivePatchLodConfig& config,
                                     std::span<const AdaptivePatchLodPatchInstance> patches) {
    validate_adaptive_patch_lod_config(config);
    AdaptivePatchLodNeighborDiagnostics diagnostics{};
    const AdaptivePatchLodSelectionSet set{patches};
    for (const AdaptivePatchLodPatchInstance& patch : patches) {
        for (std::uint32_t edge = 0; edge < 4U; ++edge) {
            const NeighborEdgeProbe probe = analyze_neighbor_edge(config, set, patch.id, edge);
            if (probe.boundary) {
                ++diagnostics.boundary_edge_count;
                continue;
            }
            if (!probe.found_neighbor) {
                continue;
            }
            ++diagnostics.edge_count;
            diagnostics.max_lod_delta = std::max(diagnostics.max_lod_delta, probe.max_delta);
            if (probe.max_delta > 0U) {
                ++diagnostics.mismatch_edge_count;
            }
        }
    }
    return diagnostics;
}

std::uint32_t adaptive_patch_lod_edge_transition_mask(
    const AdaptivePatchLodConfig& config,
    std::span<const AdaptivePatchLodPatchInstance> patches,
    AdaptivePatchLodPatchId id) {
    validate_adaptive_patch_lod_config(config);
    const AdaptivePatchLodSelectionSet set{patches};
    std::uint32_t mask = 0;
    for (std::uint32_t edge = 0; edge < 4U; ++edge) {
        const NeighborEdgeProbe probe = analyze_neighbor_edge(config, set, id, edge);
        if (probe.max_coarser_delta > 0U) {
            mask |= 1U << edge;
        }
    }
    return mask;
}

} // namespace cubey::render
