#pragma once

#include "terrain_source.h"

#include <array>
#include <cstdint>
#include <type_traits>

namespace cubey::projects::terrain {

struct alignas(16) TerrainSourceGpuBandParameters {
    std::array<float, 4> shape{};
    std::array<std::int32_t, 4> control{};
};

struct alignas(16) TerrainSourceGpuParameters {
    TerrainSourceGpuBandParameters macro{};
    TerrainSourceGpuBandParameters structure{};
    TerrainSourceGpuBandParameters detail{};
    std::array<float, 4> composition{};
    std::array<float, 4> elevation{};
    std::array<float, 4> weathering{};
    TerrainSourceGpuBandParameters v3_warp{};
    TerrainSourceGpuBandParameters v3_range{};
    TerrainSourceGpuBandParameters v3_massif{};
    TerrainSourceGpuBandParameters v3_ridge{};
    TerrainSourceGpuBandParameters v3_meso{};
    std::array<float, 4> v3_composition_0{};
    std::array<float, 4> v3_composition_1{};
    std::array<std::int32_t, 4> source_control{};
};

static_assert(std::is_trivially_copyable_v<TerrainSourceGpuParameters>);
static_assert(sizeof(TerrainSourceGpuBandParameters) == 32U);
static_assert(sizeof(TerrainSourceGpuParameters) == 352U);

[[nodiscard]] TerrainSourceGpuParameters
terrain_source_gpu_parameters(const TerrainSourceParameters& parameters);

} // namespace cubey::projects::terrain
