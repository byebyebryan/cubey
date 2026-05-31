#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace cubey::render {

struct ClipmapGrid2DConfig {
    std::uint32_t lod_levels = 1;
    std::uint32_t cells_per_axis = 1;
    float outer_half_extent = 1.0F;
    float transition_cells = 16.0F;
    float max_transition_ratio = 0.35F;
};

struct ClipmapGrid2DBounds {
    float min_x = 0.0F;
    float max_x = 0.0F;
    float min_z = 0.0F;
    float max_z = 0.0F;
};

struct ClipmapGrid2DPatch {
    std::uint32_t level = 0;
    std::uint32_t cells_x = 1;
    std::uint32_t cells_z = 1;
    ClipmapGrid2DBounds bounds{};
};

template <std::size_t MaxPatches> struct ClipmapGrid2DPatchList {
    std::array<ClipmapGrid2DPatch, MaxPatches> patches{};
    std::size_t count = 0;

    [[nodiscard]] const ClipmapGrid2DPatch* begin() const noexcept {
        return patches.data();
    }

    [[nodiscard]] const ClipmapGrid2DPatch* end() const noexcept {
        return patches.data() + count;
    }
};

[[nodiscard]] constexpr std::uint32_t clipmap_grid_2d_patch_count(std::uint32_t lod_levels) {
    return lod_levels == 0U ? 0U : 1U + (4U * (lod_levels - 1U));
}

inline void validate_clipmap_grid_2d_config(const ClipmapGrid2DConfig& config) {
    if (config.lod_levels == 0U || config.lod_levels >= 31U) {
        throw std::runtime_error("clipmap grid LOD levels out of supported range");
    }
    if (config.cells_per_axis == 0U) {
        throw std::runtime_error("clipmap grid cells per axis must be positive");
    }
    if (config.outer_half_extent <= 0.0F) {
        throw std::runtime_error("clipmap grid outer half extent must be positive");
    }
    if (config.transition_cells <= 0.0F || config.max_transition_ratio <= 0.0F) {
        throw std::runtime_error("clipmap grid transition settings must be positive");
    }
}

[[nodiscard]] inline float clipmap_grid_2d_near_half_extent(const ClipmapGrid2DConfig& config) {
    validate_clipmap_grid_2d_config(config);
    const std::uint32_t divisor = 1U << (config.lod_levels - 1U);
    return config.outer_half_extent / static_cast<float>(divisor);
}

[[nodiscard]] inline float clipmap_grid_2d_level_half_extent(const ClipmapGrid2DConfig& config,
                                                            std::uint32_t level) {
    validate_clipmap_grid_2d_config(config);
    if (level >= config.lod_levels) {
        throw std::runtime_error("clipmap grid level out of range");
    }
    return clipmap_grid_2d_near_half_extent(config) * static_cast<float>(1U << level);
}

[[nodiscard]] inline float clipmap_grid_2d_near_cell_size(const ClipmapGrid2DConfig& config) {
    return (clipmap_grid_2d_near_half_extent(config) * 2.0F) /
           static_cast<float>(config.cells_per_axis);
}

[[nodiscard]] inline float clipmap_grid_2d_level_cell_size(const ClipmapGrid2DConfig& config,
                                                          std::uint32_t level) {
    return clipmap_grid_2d_near_cell_size(config) * static_cast<float>(1U << level);
}

[[nodiscard]] inline float clipmap_grid_2d_transition_width(float coarse_cell_size,
                                                            float boundary_extent,
                                                            float transition_cells,
                                                            float max_transition_ratio) {
    if (coarse_cell_size <= 0.0F || boundary_extent <= 0.0F || transition_cells <= 0.0F ||
        max_transition_ratio <= 0.0F) {
        throw std::runtime_error("clipmap grid transition inputs must be positive");
    }
    const float preferred = coarse_cell_size * transition_cells;
    const float maximum = boundary_extent * max_transition_ratio;
    return std::max(0.001F, std::min(preferred, maximum));
}

[[nodiscard]] inline std::uint32_t clipmap_grid_2d_cells_for_span(float span,
                                                                  float target_cell_size) {
    if (span <= 0.0F || target_cell_size <= 0.0F) {
        throw std::runtime_error("clipmap grid patch span must be positive");
    }
    return std::max(1U, static_cast<std::uint32_t>(std::ceil(span / target_cell_size)));
}

template <std::size_t MaxPatches>
inline void clipmap_grid_2d_add_patch(ClipmapGrid2DPatchList<MaxPatches>& list,
                                      std::uint32_t level, ClipmapGrid2DBounds bounds,
                                      float target_cell_size) {
    if (list.count >= list.patches.size()) {
        throw std::runtime_error("clipmap grid patch list overflow");
    }
    if (bounds.min_x >= bounds.max_x || bounds.min_z >= bounds.max_z) {
        throw std::runtime_error("clipmap grid patch bounds must be ordered");
    }
    const float span_x = bounds.max_x - bounds.min_x;
    const float span_z = bounds.max_z - bounds.min_z;
    list.patches[list.count++] = ClipmapGrid2DPatch{
        .level = level,
        .cells_x = clipmap_grid_2d_cells_for_span(span_x, target_cell_size),
        .cells_z = clipmap_grid_2d_cells_for_span(span_z, target_cell_size),
        .bounds = bounds,
    };
}

template <std::size_t MaxPatches>
[[nodiscard]] inline ClipmapGrid2DPatchList<MaxPatches>
clipmap_grid_2d_patches(const ClipmapGrid2DConfig& config) {
    validate_clipmap_grid_2d_config(config);
    if (clipmap_grid_2d_patch_count(config.lod_levels) > MaxPatches) {
        throw std::runtime_error("clipmap grid patch list capacity is too small");
    }

    ClipmapGrid2DPatchList<MaxPatches> list{};
    for (std::uint32_t offset = 0; offset < config.lod_levels; ++offset) {
        const std::uint32_t level = config.lod_levels - 1U - offset;
        const float outer = clipmap_grid_2d_level_half_extent(config, level);
        const float target_cell_size = clipmap_grid_2d_level_cell_size(config, level);
        if (level == 0U) {
            clipmap_grid_2d_add_patch(list, level, {-outer, outer, -outer, outer},
                                      target_cell_size);
            continue;
        }

        const float inner = clipmap_grid_2d_level_half_extent(config, level - 1U);
        const float overlap =
            clipmap_grid_2d_transition_width(target_cell_size, inner, config.transition_cells,
                                             config.max_transition_ratio);
        clipmap_grid_2d_add_patch(list, level, {-outer, outer, inner - overlap, outer},
                                  target_cell_size);
        clipmap_grid_2d_add_patch(list, level, {-outer, outer, -outer, -inner + overlap},
                                  target_cell_size);
        clipmap_grid_2d_add_patch(list, level, {-outer, -inner + overlap, -inner, inner},
                                  target_cell_size);
        clipmap_grid_2d_add_patch(list, level, {inner - overlap, outer, -inner, inner},
                                  target_cell_size);
    }
    return list;
}

} // namespace cubey::render
