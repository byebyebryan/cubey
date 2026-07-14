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
        .v2_1_composition =
            {
                static_cast<float>(parameters.v2_1.core_detail_octaves),
                parameters.v2_1.fine_detail_strength,
                parameters.v2_1.fine_detail_cap_m,
                0.0F,
            },
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
        .v3_warp = gpu_band(parameters.v3.warp),
        .v3_range = gpu_band(parameters.v3.range),
        .v3_massif = gpu_band(parameters.v3.massif),
        .v3_ridge = gpu_band(parameters.v3.ridge),
        .v3_meso = gpu_band(parameters.v3.meso),
        .v3_composition_0 =
            {
                parameters.v3.warp_strength_m,
                parameters.v3.valley_ratio,
                parameters.v3.valley_cap_m,
                parameters.v3.ridge_ratio,
            },
        .v3_composition_1 =
            {
                parameters.v3.ridge_cap_m,
                parameters.v3.meso_ratio,
                parameters.v3.meso_cap_m,
                0.0F,
            },
        .source_control = {static_cast<std::int32_t>(parameters.version), 0, 0, 0},
    };
}

} // namespace cubey::projects::terrain
