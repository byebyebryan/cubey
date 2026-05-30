#pragma once

#include "ocean_ref_config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace cubey::projects::ocean_ref {

inline constexpr std::uint32_t kOceanRefMaxMeshPatches =
    1U + (4U * (kOceanRefMaxMeshLodLevels - 1U));
inline constexpr float kOceanRefMeshTransitionCells = 16.0F;
inline constexpr float kOceanRefMeshMaxTransitionRatio = 0.35F;

struct OceanRefMeshPatchBounds {
    float min_x = 0.0F;
    float max_x = 0.0F;
    float min_z = 0.0F;
    float max_z = 0.0F;
};

struct OceanRefMeshPatch {
    std::uint32_t level = 0;
    std::uint32_t cells_x = 1;
    std::uint32_t cells_z = 1;
    OceanRefMeshPatchBounds bounds{};
};

struct OceanRefMeshPatchList {
    std::array<OceanRefMeshPatch, kOceanRefMaxMeshPatches> patches{};
    std::size_t count = 0;

    [[nodiscard]] const OceanRefMeshPatch* begin() const noexcept {
        return patches.data();
    }

    [[nodiscard]] const OceanRefMeshPatch* end() const noexcept {
        return patches.data() + count;
    }
};

[[nodiscard]] inline float ocean_ref_mesh_near_half_extent(const OceanRefConfig& config) {
    if (config.mesh_lod_levels == 0U) {
        throw std::runtime_error("ocean_ref mesh LOD levels out of supported range");
    }
    const std::uint32_t divisor = 1U << (config.mesh_lod_levels - 1U);
    return config.mesh_extent / static_cast<float>(divisor);
}

[[nodiscard]] inline float ocean_ref_mesh_level_half_extent(const OceanRefConfig& config,
                                                            std::uint32_t level) {
    if (level >= config.mesh_lod_levels) {
        throw std::runtime_error("ocean_ref mesh level out of range");
    }
    return ocean_ref_mesh_near_half_extent(config) * static_cast<float>(1U << level);
}

[[nodiscard]] inline float ocean_ref_mesh_near_cell_size(const OceanRefConfig& config) {
    return (ocean_ref_mesh_near_half_extent(config) * 2.0F) / static_cast<float>(config.mesh_cells);
}

[[nodiscard]] inline float ocean_ref_mesh_level_cell_size(const OceanRefConfig& config,
                                                          std::uint32_t level) {
    return ocean_ref_mesh_near_cell_size(config) * static_cast<float>(1U << level);
}

[[nodiscard]] inline float ocean_ref_mesh_transition_width(float coarse_cell_size,
                                                           float boundary_extent) {
    if (coarse_cell_size <= 0.0F || boundary_extent <= 0.0F) {
        throw std::runtime_error("ocean_ref mesh transition inputs must be positive");
    }
    const float preferred = coarse_cell_size * kOceanRefMeshTransitionCells;
    const float maximum = boundary_extent * kOceanRefMeshMaxTransitionRatio;
    return std::max(0.001F, std::min(preferred, maximum));
}

[[nodiscard]] inline std::uint32_t ocean_ref_mesh_cells_for_span(float span,
                                                                 float target_cell_size) {
    if (span <= 0.0F || target_cell_size <= 0.0F) {
        throw std::runtime_error("ocean_ref mesh patch span must be positive");
    }
    return std::max(1U, static_cast<std::uint32_t>(std::ceil(span / target_cell_size)));
}

[[nodiscard]] inline std::uint32_t
ocean_ref_mesh_patch_vertex_count(const OceanRefMeshPatch& patch) {
    return patch.cells_x * patch.cells_z * 6U;
}

[[nodiscard]] inline std::uint32_t
ocean_ref_mesh_patch_triangle_count(const OceanRefMeshPatch& patch) {
    return patch.cells_x * patch.cells_z * 2U;
}

inline void ocean_ref_mesh_add_patch(OceanRefMeshPatchList& list, std::uint32_t level,
                                     OceanRefMeshPatchBounds bounds, float target_cell_size) {
    if (list.count >= list.patches.size()) {
        throw std::runtime_error("ocean_ref mesh patch list overflow");
    }
    if (bounds.min_x >= bounds.max_x || bounds.min_z >= bounds.max_z) {
        throw std::runtime_error("ocean_ref mesh patch bounds must be ordered");
    }
    const float span_x = bounds.max_x - bounds.min_x;
    const float span_z = bounds.max_z - bounds.min_z;
    list.patches[list.count++] = OceanRefMeshPatch{
        .level = level,
        .cells_x = ocean_ref_mesh_cells_for_span(span_x, target_cell_size),
        .cells_z = ocean_ref_mesh_cells_for_span(span_z, target_cell_size),
        .bounds = bounds,
    };
}

[[nodiscard]] inline OceanRefMeshPatchList
ocean_ref_mesh_clipmap_patches(const OceanRefConfig& config) {
    validate_ocean_ref_config(config);

    OceanRefMeshPatchList list{};
    for (std::uint32_t offset = 0; offset < config.mesh_lod_levels; ++offset) {
        const std::uint32_t level = config.mesh_lod_levels - 1U - offset;
        const float outer = ocean_ref_mesh_level_half_extent(config, level);
        const float target_cell_size = ocean_ref_mesh_level_cell_size(config, level);
        if (level == 0U) {
            ocean_ref_mesh_add_patch(list, level, {-outer, outer, -outer, outer}, target_cell_size);
            continue;
        }

        const float inner = ocean_ref_mesh_level_half_extent(config, level - 1U);
        const float overlap = ocean_ref_mesh_transition_width(target_cell_size, inner);
        ocean_ref_mesh_add_patch(list, level, {-outer, outer, inner - overlap, outer},
                                 target_cell_size);
        ocean_ref_mesh_add_patch(list, level, {-outer, outer, -outer, -inner + overlap},
                                 target_cell_size);
        ocean_ref_mesh_add_patch(list, level, {-outer, -inner + overlap, -inner, inner},
                                 target_cell_size);
        ocean_ref_mesh_add_patch(list, level, {inner - overlap, outer, -inner, inner},
                                 target_cell_size);
    }
    return list;
}

[[nodiscard]] inline std::uint32_t ocean_ref_mesh_patch_count(const OceanRefConfig& config) {
    return 1U + (4U * (config.mesh_lod_levels - 1U));
}

[[nodiscard]] inline std::uint32_t
ocean_ref_mesh_total_triangle_count(const OceanRefConfig& config) {
    const OceanRefMeshPatchList patches = ocean_ref_mesh_clipmap_patches(config);
    std::uint32_t triangles = 0;
    for (const OceanRefMeshPatch& patch : patches) {
        triangles += ocean_ref_mesh_patch_triangle_count(patch);
    }
    return triangles;
}

[[nodiscard]] inline std::uint32_t ocean_ref_mesh_total_vertex_count(const OceanRefConfig& config) {
    const OceanRefMeshPatchList patches = ocean_ref_mesh_clipmap_patches(config);
    std::uint32_t vertices = 0;
    for (const OceanRefMeshPatch& patch : patches) {
        vertices += ocean_ref_mesh_patch_vertex_count(patch);
    }
    return vertices;
}

} // namespace cubey::projects::ocean_ref
