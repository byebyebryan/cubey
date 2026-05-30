#pragma once

#include "ocean_config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace cubey::projects::ocean_legacy {

inline constexpr std::uint32_t kOceanMaxMeshPatches =
    1U + (4U * (kOceanMaxMeshLodLevels - 1U));
inline constexpr float kOceanMeshTransitionCells = 16.0F;
inline constexpr float kOceanMeshMaxTransitionRatio = 0.35F;

struct OceanMeshPatchBounds {
    float min_x = 0.0F;
    float max_x = 0.0F;
    float min_z = 0.0F;
    float max_z = 0.0F;
};

struct OceanMeshPatch {
    std::uint32_t level = 0;
    std::uint32_t cells_x = 1;
    std::uint32_t cells_z = 1;
    OceanMeshPatchBounds bounds{};
};

struct OceanMeshPatchList {
    std::array<OceanMeshPatch, kOceanMaxMeshPatches> patches{};
    std::size_t count = 0;

    [[nodiscard]] const OceanMeshPatch* begin() const noexcept {
        return patches.data();
    }

    [[nodiscard]] const OceanMeshPatch* end() const noexcept {
        return patches.data() + count;
    }
};

[[nodiscard]] inline float ocean_mesh_near_half_extent(const OceanConfig& config) {
    if (config.mesh_lod_levels == 0U) {
        throw std::runtime_error("ocean mesh LOD levels out of supported range");
    }
    const std::uint32_t divisor = 1U << (config.mesh_lod_levels - 1U);
    return config.mesh_extent / static_cast<float>(divisor);
}

[[nodiscard]] inline float ocean_mesh_level_half_extent(const OceanConfig& config,
                                                        std::uint32_t level) {
    if (level >= config.mesh_lod_levels) {
        throw std::runtime_error("ocean mesh level out of range");
    }
    return ocean_mesh_near_half_extent(config) * static_cast<float>(1U << level);
}

[[nodiscard]] inline float ocean_mesh_near_cell_size(const OceanConfig& config) {
    return (ocean_mesh_near_half_extent(config) * 2.0F) /
           static_cast<float>(config.mesh_cells);
}

[[nodiscard]] inline float ocean_mesh_level_cell_size(const OceanConfig& config,
                                                      std::uint32_t level) {
    return ocean_mesh_near_cell_size(config) * static_cast<float>(1U << level);
}

[[nodiscard]] inline float ocean_mesh_transition_width(float coarse_cell_size,
                                                       float boundary_extent) {
    if (coarse_cell_size <= 0.0F || boundary_extent <= 0.0F) {
        throw std::runtime_error("ocean mesh transition inputs must be positive");
    }
    const float preferred = coarse_cell_size * kOceanMeshTransitionCells;
    const float maximum = boundary_extent * kOceanMeshMaxTransitionRatio;
    return std::max(0.001F, std::min(preferred, maximum));
}

[[nodiscard]] inline std::uint32_t ocean_mesh_cells_for_span(float span, float target_cell_size) {
    if (span <= 0.0F || target_cell_size <= 0.0F) {
        throw std::runtime_error("ocean mesh patch span must be positive");
    }
    return std::max(1U, static_cast<std::uint32_t>(std::ceil(span / target_cell_size)));
}

[[nodiscard]] inline std::uint32_t ocean_mesh_patch_vertex_count(const OceanMeshPatch& patch) {
    return patch.cells_x * patch.cells_z * 6U;
}

[[nodiscard]] inline std::uint32_t ocean_mesh_patch_triangle_count(const OceanMeshPatch& patch) {
    return patch.cells_x * patch.cells_z * 2U;
}

inline void ocean_mesh_add_patch(OceanMeshPatchList& list, std::uint32_t level,
                                 OceanMeshPatchBounds bounds, float target_cell_size) {
    if (list.count >= list.patches.size()) {
        throw std::runtime_error("ocean mesh patch list overflow");
    }
    if (bounds.min_x >= bounds.max_x || bounds.min_z >= bounds.max_z) {
        throw std::runtime_error("ocean mesh patch bounds must be ordered");
    }
    const float span_x = bounds.max_x - bounds.min_x;
    const float span_z = bounds.max_z - bounds.min_z;
    list.patches[list.count++] = OceanMeshPatch{
        .level = level,
        .cells_x = ocean_mesh_cells_for_span(span_x, target_cell_size),
        .cells_z = ocean_mesh_cells_for_span(span_z, target_cell_size),
        .bounds = bounds,
    };
}

[[nodiscard]] inline OceanMeshPatchList ocean_mesh_clipmap_patches(const OceanConfig& config) {
    validate_ocean_config(config);

    OceanMeshPatchList list{};
    for (std::uint32_t offset = 0; offset < config.mesh_lod_levels; ++offset) {
        const std::uint32_t level = config.mesh_lod_levels - 1U - offset;
        const float outer = ocean_mesh_level_half_extent(config, level);
        const float target_cell_size = ocean_mesh_level_cell_size(config, level);
        if (level == 0U) {
            ocean_mesh_add_patch(list, level, {-outer, outer, -outer, outer}, target_cell_size);
            continue;
        }

        const float inner = ocean_mesh_level_half_extent(config, level - 1U);
        const float overlap = ocean_mesh_transition_width(target_cell_size, inner);
        ocean_mesh_add_patch(list, level, {-outer, outer, inner - overlap, outer},
                             target_cell_size);
        ocean_mesh_add_patch(list, level, {-outer, outer, -outer, -inner + overlap},
                             target_cell_size);
        ocean_mesh_add_patch(list, level, {-outer, -inner + overlap, -inner, inner},
                             target_cell_size);
        ocean_mesh_add_patch(list, level, {inner - overlap, outer, -inner, inner},
                             target_cell_size);
    }
    return list;
}

[[nodiscard]] inline std::uint32_t ocean_mesh_patch_count(const OceanConfig& config) {
    return 1U + (4U * (config.mesh_lod_levels - 1U));
}

[[nodiscard]] inline std::uint32_t ocean_mesh_total_triangle_count(const OceanConfig& config) {
    const OceanMeshPatchList patches = ocean_mesh_clipmap_patches(config);
    std::uint32_t triangles = 0;
    for (const OceanMeshPatch& patch : patches) {
        triangles += ocean_mesh_patch_triangle_count(patch);
    }
    return triangles;
}

[[nodiscard]] inline std::uint32_t ocean_mesh_total_vertex_count(const OceanConfig& config) {
    const OceanMeshPatchList patches = ocean_mesh_clipmap_patches(config);
    std::uint32_t vertices = 0;
    for (const OceanMeshPatch& patch : patches) {
        vertices += ocean_mesh_patch_vertex_count(patch);
    }
    return vertices;
}

} // namespace cubey::projects::ocean_legacy
