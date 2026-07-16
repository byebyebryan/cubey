#pragma once

#include "terrain_backdrop_product.h"

#include <cubey/core/run_config.h>

#include <cstdint>

namespace cubey::projects::terrain {

struct TerrainAppOptions {
    const TerrainHeightSource* backdrop_source = nullptr;
    std::uint32_t backdrop_render_stride = 0U;
    TerrainBackdropCenterMode backdrop_center_mode = TerrainBackdropCenterMode::Cutout;
};

int run_terrain(const cubey::RunConfig& config);
int run_terrain_with_options(const cubey::RunConfig& config, TerrainAppOptions options);

} // namespace cubey::projects::terrain
