#include "terrain_source_gpu.h"

namespace cubey::projects::terrain {
namespace {

[[nodiscard]] TerrainSourceGpuBandParameters gpu_band(const TerrainSourceBandParameters& band) {
    return {
        .shape = {band.frequency, band.lacunarity, band.gain, band.ridge_mix},
        .control = {band.seed, static_cast<std::int32_t>(band.octaves), 0, 0},
    };
}

} // namespace

TerrainSourceGpuParameters
terrain_source_gpu_parameters(const TerrainSourceParameters& parameters) {
    validate_terrain_source_parameters(parameters);
    return {
        .macro = gpu_band(parameters.macro),
        .structure = gpu_band(parameters.structure),
        .detail = gpu_band(parameters.detail),
        .composition = {parameters.macro_weight, parameters.structure_weight,
                        parameters.detail_weight, parameters.elevation_bias},
        .elevation = {parameters.base_height_m, parameters.height_scale_m,
                      parameters.elevation_power, parameters.gradient_step_m},
        .weathering =
            {
                parameters.weathering_radius_m,
                parameters.weathering_max_delta_m,
                parameters.weathering_strength,
                parameters.weathering == TerrainWeatheringMode::Local ? 1.0F : 0.0F,
            },
    };
}

} // namespace cubey::projects::terrain
