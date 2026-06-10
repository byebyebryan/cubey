#pragma once

#include <cubey/core/run_config.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cubey::projects::ocean {

enum class OceanRenderView : std::uint32_t {
    Final = 0,
    Height = 1,
    Displacement = 2,
    Normal = 3,
    Foam = 4,
    FoamSource = 5,
    FoamHistory = 6,
    FoamCore = 7,
    FoamCandidate = 8,
    FoamDetail = 9,
    Lod = 10,
    SkyRadiance = 11,
    Reflection = 12,
    DirectLight = 13,
    AmbientLight = 14,
    Exposure = 15,
    FoamRaw = 16,
    FoamLit = 17,
    TerrainDepth = 18,
    TerrainShore = 19,
    TerrainSlope = 20,
    Curvature = 21,
};

enum class OceanFieldPrecision : std::uint32_t {
    Full = 0,
    Half = 1,
};

enum class OceanSurfaceMode : std::uint32_t {
    Flat = 0,
    CurvedFar = 1,
};

inline constexpr std::array<OceanRenderView, 22> kOceanRenderViews{
    OceanRenderView::Final,        OceanRenderView::Height,       OceanRenderView::Displacement,
    OceanRenderView::Normal,       OceanRenderView::Foam,         OceanRenderView::FoamSource,
    OceanRenderView::FoamHistory,  OceanRenderView::FoamCore,     OceanRenderView::FoamCandidate,
    OceanRenderView::FoamDetail,   OceanRenderView::Lod,          OceanRenderView::SkyRadiance,
    OceanRenderView::Reflection,   OceanRenderView::DirectLight,  OceanRenderView::AmbientLight,
    OceanRenderView::Exposure,     OceanRenderView::FoamRaw,      OceanRenderView::FoamLit,
    OceanRenderView::TerrainDepth, OceanRenderView::TerrainShore, OceanRenderView::TerrainSlope,
    OceanRenderView::Curvature,
};
inline constexpr std::array<OceanFieldPrecision, 2> kOceanFieldPrecisions{
    OceanFieldPrecision::Full,
    OceanFieldPrecision::Half,
};
inline constexpr std::array<OceanSurfaceMode, 2> kOceanSurfaceModes{
    OceanSurfaceMode::Flat,
    OceanSurfaceMode::CurvedFar,
};

inline constexpr std::array<std::uint32_t, 4> kOceanSupportedMapSizes{128U, 256U, 512U, 1024U};
inline constexpr std::uint32_t kOceanDefaultMapSize = 512U;
inline constexpr std::uint32_t kOceanCascadeCount = 5U;
inline constexpr std::uint32_t kOceanSpectrumFieldCount = 2U;
inline constexpr std::uint32_t kOceanMinMeshCells = 32U;
inline constexpr std::uint32_t kOceanMaxMeshCells = 512U;
inline constexpr std::uint32_t kOceanMinMeshLodLevels = 1U;
inline constexpr std::uint32_t kOceanMaxMeshLodLevels = 6U;
inline constexpr float kOceanPi = 3.14159265358979323846F;
inline constexpr float kOceanCascadeSmallestWaveMultiplier = 4.0F;
inline constexpr float kOceanCascadeDistanceFadeStartWaves = 10.0F;
inline constexpr float kOceanCascadeDistanceFadeEndWaves = 34.0F;
inline constexpr float kOceanCascadeSurfaceFadeStartWaves = 12.0F;
inline constexpr float kOceanCascadeSurfaceFadeEndWaves = 40.0F;
inline constexpr float kOceanCascadeMeshFullTileCellDivisor = 8.0F;
inline constexpr float kOceanCascadeMeshZeroTileCellDivisor = 3.0F;

struct OceanCascadeConfig {
    float tile_length = 88.0F;
    float displacement_scale = 1.0F;
    float normal_scale = 1.0F;
    float wind_speed = 10.0F;
    float wind_direction_degrees = 20.0F;
    float fetch_length_km = 150.0F;
    float swell = 0.8F;
    float spread = 0.2F;
    float detail = 1.0F;
    float whitecap = 0.5F;
    float foam_amount = 8.0F;
    float domain_min_waves = 0.0F;
    std::int32_t seed_x = 1337;
    std::int32_t seed_y = 4919;
    float time_offset = 120.0F;

    friend bool operator==(const OceanCascadeConfig&, const OceanCascadeConfig&) = default;
};

struct OceanConfig {
    std::uint32_t mesh_cells = 512U;
    std::uint32_t mesh_lod_levels = 5U;
    float mesh_extent = 5600.0F;
    float horizon_fog = 0.50F;
    bool horizon_auto_extent = true;
    float horizon_extent_margin = 1.25F;
    float horizon_target_near_cell_m = 2.0F;
    OceanSurfaceMode surface_mode = OceanSurfaceMode::CurvedFar;
    float planet_radius_scale = 1.0F;
    float curvature_start_ratio = 0.25F;
    float curvature_end_ratio = 0.75F;
    float curvature_strength = 1.0F;

    std::uint32_t map_size = kOceanDefaultMapSize;
    float depth = 20.0F;
    float roughness = 0.4F;
    float normal_strength = 1.0F;
    float exposure = 0.0F;
    float water_color_r = 0.1F;
    float water_color_g = 0.15F;
    float water_color_b = 0.18F;
    float foam_color_r = 0.73F;
    float foam_color_g = 0.67F;
    float foam_color_b = 0.62F;
    float foam_density = 3.15F;
    float foam_sharpness = 0.62F;
    float surface_shape_strength = 1.0F;
    float surface_foam_strength = 1.0F;
    float foam_history_strength = 1.0F;
    float atmosphere_material_strength = 1.0F;
    float atmosphere_sky_strength = 1.0F;
    float atmosphere_reflection_strength = 1.0F;
    float atmosphere_light_strength = 1.0F;
    float foam_lighting_strength = 1.0F;
    float self_shadow_strength = 0.45F;
    float self_shadow_distance = 44.0F;
    float self_shadow_bias = 0.18F;
    std::uint32_t self_shadow_steps = 8U;
    float terrain_foam_strength = 1.0F;
    float shape_fade_distance_scale = 1.0F;
    float normal_fade_distance_scale = 1.0F;
    float foam_fade_distance_scale = 1.0F;
    bool spectral_domains_enabled = true;
    bool terrain_fields_enabled = false;
    OceanFieldPrecision field_precision = OceanFieldPrecision::Half;
    std::array<bool, kOceanCascadeCount> cascade_enabled{true, true, false, false, false};
    std::array<std::uint32_t, kOceanCascadeCount> cascade_map_sizes{0U, 0U, 0U, 0U, 0U};
    std::array<std::uint32_t, kOceanCascadeCount> cascade_update_intervals{1U, 1U, 1U, 1U, 1U};
    OceanRenderView render_view = OceanRenderView::Final;
    std::array<OceanCascadeConfig, kOceanCascadeCount> cascades{
        OceanCascadeConfig{
            .tile_length = 88.0F,
            .displacement_scale = 1.35F,
            .normal_scale = 1.35F,
            .wind_speed = 18.0F,
            .wind_direction_degrees = 20.0F,
            .fetch_length_km = 350.0F,
            .swell = 1.0F,
            .spread = 0.14F,
            .detail = 1.0F,
            .whitecap = 0.50F,
            .foam_amount = 5.80F,
            .domain_min_waves = 0.0F,
            .seed_x = 1337,
            .seed_y = 4919,
            .time_offset = 120.0F,
        },
        OceanCascadeConfig{
            .tile_length = 57.0F,
            .displacement_scale = 1.08F,
            .normal_scale = 1.35F,
            .wind_speed = 16.0F,
            .wind_direction_degrees = 17.0F,
            .fetch_length_km = 330.0F,
            .swell = 0.95F,
            .spread = 0.25F,
            .detail = 1.0F,
            .whitecap = 0.48F,
            .foam_amount = 4.80F,
            .domain_min_waves = 0.0F,
            .seed_x = -2713,
            .seed_y = 8128,
            .time_offset = 123.14159F,
        },
        OceanCascadeConfig{
            .tile_length = 1531.0F,
            .displacement_scale = 0.55F,
            .normal_scale = 0.16F,
            .wind_speed = 32.0F,
            .wind_direction_degrees = 17.0F,
            .fetch_length_km = 1200.0F,
            .swell = 1.15F,
            .spread = 0.38F,
            .detail = 0.38F,
            .whitecap = 0.12F,
            .foam_amount = 0.0F,
            .domain_min_waves = 3.0F,
            .seed_x = -5441,
            .seed_y = 2203,
            .time_offset = 131.5F,
        },
        OceanCascadeConfig{
            .tile_length = 421.0F,
            .displacement_scale = 0.95F,
            .normal_scale = 0.36F,
            .wind_speed = 30.0F,
            .wind_direction_degrees = 19.0F,
            .fetch_length_km = 1100.0F,
            .swell = 1.10F,
            .spread = 0.28F,
            .detail = 0.70F,
            .whitecap = 0.28F,
            .foam_amount = 0.90F,
            .domain_min_waves = 2.0F,
            .seed_x = 9311,
            .seed_y = -1733,
            .time_offset = 117.0F,
        },
        OceanCascadeConfig{
            .tile_length = 16.0F,
            .displacement_scale = 0.0F,
            .normal_scale = 0.50F,
            .wind_speed = 30.0F,
            .wind_direction_degrees = 20.0F,
            .fetch_length_km = 850.0F,
            .swell = 0.9F,
            .spread = 0.25F,
            .detail = 1.0F,
            .whitecap = 0.44F,
            .foam_amount = 2.20F,
            .domain_min_waves = 3.0F,
            .seed_x = 6619,
            .seed_y = -3544,
            .time_offset = 126.28318F,
        },
    };

    friend bool operator==(const OceanConfig&, const OceanConfig&) = default;
};

struct OceanCascadeDomain {
    float low_k = 0.0F;
    float high_k = 0.0F;
    float low_wavelength = 0.0F;
    float high_wavelength = 0.0F;
    bool active = false;

    friend bool operator==(const OceanCascadeDomain&, const OceanCascadeDomain&) = default;
};

struct OceanCascadeLodBand {
    float displacement_fade_start = 0.0F;
    float displacement_fade_end = 0.0F;
    float surface_fade_start = 0.0F;
    float surface_fade_end = 0.0F;
    float mesh_cell_full = 0.0F;
    float mesh_cell_zero = 0.0F;

    friend bool operator==(const OceanCascadeLodBand&, const OceanCascadeLodBand&) = default;
};

[[nodiscard]] inline const char* ocean_render_view_name(OceanRenderView view) {
    switch (view) {
    case OceanRenderView::Final:
        return "final";
    case OceanRenderView::Height:
        return "height";
    case OceanRenderView::Displacement:
        return "displacement";
    case OceanRenderView::Normal:
        return "normal";
    case OceanRenderView::Foam:
        return "foam";
    case OceanRenderView::FoamSource:
        return "foam-source";
    case OceanRenderView::FoamHistory:
        return "foam-history";
    case OceanRenderView::FoamCore:
        return "foam-core";
    case OceanRenderView::FoamCandidate:
        return "foam-candidate";
    case OceanRenderView::FoamDetail:
        return "foam-detail";
    case OceanRenderView::Lod:
        return "lod";
    case OceanRenderView::SkyRadiance:
        return "sky-radiance";
    case OceanRenderView::Reflection:
        return "reflection";
    case OceanRenderView::DirectLight:
        return "direct-light";
    case OceanRenderView::AmbientLight:
        return "ambient-light";
    case OceanRenderView::Exposure:
        return "exposure";
    case OceanRenderView::FoamRaw:
        return "foam-raw";
    case OceanRenderView::FoamLit:
        return "foam-lit";
    case OceanRenderView::TerrainDepth:
        return "terrain-depth";
    case OceanRenderView::TerrainShore:
        return "terrain-shore";
    case OceanRenderView::TerrainSlope:
        return "terrain-slope";
    case OceanRenderView::Curvature:
        return "curvature";
    }
    return "final";
}

[[nodiscard]] inline const char* ocean_field_precision_name(OceanFieldPrecision precision) {
    switch (precision) {
    case OceanFieldPrecision::Full:
        return "full";
    case OceanFieldPrecision::Half:
        return "half";
    }
    return "full";
}

[[nodiscard]] inline const char* ocean_surface_mode_name(OceanSurfaceMode mode) {
    switch (mode) {
    case OceanSurfaceMode::Flat:
        return "flat";
    case OceanSurfaceMode::CurvedFar:
        return "curved-far";
    }
    return "curved-far";
}

[[nodiscard]] inline OceanFieldPrecision
ocean_field_precision_from_name(std::string_view name) {
    if (name.empty() || name == "half") {
        return OceanFieldPrecision::Half;
    }
    if (name == "full") {
        return OceanFieldPrecision::Full;
    }
    throw std::runtime_error("unknown ocean field precision: " + std::string(name));
}

[[nodiscard]] inline OceanSurfaceMode ocean_surface_mode_from_name(std::string_view name) {
    if (name.empty() || name == "curved-far" || name == "curved") {
        return OceanSurfaceMode::CurvedFar;
    }
    if (name == "flat") {
        return OceanSurfaceMode::Flat;
    }
    throw std::runtime_error("unknown ocean surface mode: " + std::string(name));
}

[[nodiscard]] inline OceanRenderView ocean_render_view_from_name(std::string_view name) {
    if (name.empty()) {
        return OceanRenderView::Final;
    }
    for (const OceanRenderView view : kOceanRenderViews) {
        if (name == ocean_render_view_name(view)) {
            return view;
        }
    }
    throw std::runtime_error("unknown ocean render view: " + std::string(name));
}

[[nodiscard]] inline OceanRenderView next_ocean_render_view(OceanRenderView view) {
    for (std::size_t index = 0; index < kOceanRenderViews.size(); ++index) {
        if (kOceanRenderViews[index] == view) {
            return kOceanRenderViews[(index + 1U) % kOceanRenderViews.size()];
        }
    }
    return OceanRenderView::Final;
}

[[nodiscard]] inline bool ocean_is_power_of_two(std::uint32_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

[[nodiscard]] inline bool ocean_is_supported_map_size(std::uint32_t value) {
    for (const std::uint32_t supported : kOceanSupportedMapSizes) {
        if (value == supported) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline std::uint32_t ocean_mesh_vertex_count(const OceanConfig& config) {
    if (config.mesh_cells < kOceanMinMeshCells || config.mesh_cells > kOceanMaxMeshCells) {
        throw std::runtime_error("ocean mesh cells out of supported range");
    }
    return config.mesh_cells * config.mesh_cells * 6U;
}

[[nodiscard]] inline const OceanCascadeConfig& ocean_cascade(const OceanConfig& config,
                                                             std::uint32_t cascade) {
    if (cascade >= kOceanCascadeCount) {
        throw std::runtime_error("ocean cascade index out of range");
    }
    return config.cascades[cascade];
}

[[nodiscard]] inline bool ocean_cascade_enabled(const OceanConfig& config, std::uint32_t cascade) {
    if (cascade >= kOceanCascadeCount) {
        throw std::runtime_error("ocean cascade index out of range");
    }
    return config.cascade_enabled[cascade];
}

[[nodiscard]] inline std::uint32_t ocean_cascade_map_size(const OceanConfig& config,
                                                          std::uint32_t cascade) {
    if (cascade >= kOceanCascadeCount) {
        throw std::runtime_error("ocean cascade index out of range");
    }
    const std::uint32_t override_size = config.cascade_map_sizes[cascade];
    return override_size == 0U ? config.map_size : override_size;
}

[[nodiscard]] inline std::uint32_t ocean_cascade_update_interval(const OceanConfig& config,
                                                                 std::uint32_t cascade) {
    if (cascade >= kOceanCascadeCount) {
        throw std::runtime_error("ocean cascade index out of range");
    }
    return std::max(1U, config.cascade_update_intervals[cascade]);
}

[[nodiscard]] inline OceanCascadeLodBand ocean_cascade_lod_band(const OceanConfig& config,
                                                                std::uint32_t cascade) {
    const float tile_length = ocean_cascade(config, cascade).tile_length;
    if (tile_length <= 0.0F) {
        throw std::runtime_error("ocean cascade tile length must be positive");
    }
    const float shape_fade = std::max(config.shape_fade_distance_scale, 0.001F);
    return {
        .displacement_fade_start =
            tile_length * kOceanCascadeDistanceFadeStartWaves * shape_fade,
        .displacement_fade_end = tile_length * kOceanCascadeDistanceFadeEndWaves * shape_fade,
        .surface_fade_start = tile_length * kOceanCascadeSurfaceFadeStartWaves * shape_fade,
        .surface_fade_end = tile_length * kOceanCascadeSurfaceFadeEndWaves * shape_fade,
        .mesh_cell_full = tile_length / kOceanCascadeMeshFullTileCellDivisor,
        .mesh_cell_zero = tile_length / kOceanCascadeMeshZeroTileCellDivisor,
    };
}

[[nodiscard]] inline float ocean_cascade_lod_smoothstep(float edge0, float edge1, float value) {
    if (edge1 <= edge0) {
        return value >= edge1 ? 1.0F : 0.0F;
    }
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] inline float ocean_cascade_distance_lod_weight(const OceanConfig& config,
                                                             std::uint32_t cascade,
                                                             float camera_distance_m,
                                                             float start_waves,
                                                             float end_waves,
                                                             float fade_scale) {
    const float tile_length = std::max(ocean_cascade(config, cascade).tile_length, 0.001F);
    const float scale = std::max(fade_scale, 0.001F);
    const float start = tile_length * start_waves * scale;
    const float end = tile_length * end_waves * scale;
    return 1.0F - ocean_cascade_lod_smoothstep(start, std::max(end, start + 0.001F),
                                               std::max(camera_distance_m, 0.0F));
}

[[nodiscard]] inline float ocean_cascade_mesh_lod_weight(const OceanConfig& config,
                                                         std::uint32_t cascade,
                                                         float mesh_cell_size_m) {
    const float tile_length = std::max(ocean_cascade(config, cascade).tile_length, 0.001F);
    const float full_cell = tile_length / kOceanCascadeMeshFullTileCellDivisor;
    const float zero_cell = tile_length / kOceanCascadeMeshZeroTileCellDivisor;
    return 1.0F - ocean_cascade_lod_smoothstep(full_cell, std::max(zero_cell, full_cell + 0.001F),
                                               std::max(mesh_cell_size_m, 0.001F));
}

[[nodiscard]] inline float ocean_cascade_displacement_lod_weight(const OceanConfig& config,
                                                                 std::uint32_t cascade,
                                                                 float camera_distance_m,
                                                                 float mesh_cell_size_m) {
    return ocean_cascade_distance_lod_weight(config, cascade,
                                             camera_distance_m,
                                             kOceanCascadeDistanceFadeStartWaves,
                                             kOceanCascadeDistanceFadeEndWaves,
                                             config.shape_fade_distance_scale) *
           ocean_cascade_mesh_lod_weight(config, cascade, mesh_cell_size_m);
}

[[nodiscard]] inline float ocean_cascade_surface_lod_weight(const OceanConfig& config,
                                                            std::uint32_t cascade,
                                                            float camera_distance_m) {
    return ocean_cascade_distance_lod_weight(config, cascade,
                                             camera_distance_m,
                                             kOceanCascadeSurfaceFadeStartWaves,
                                             kOceanCascadeSurfaceFadeEndWaves,
                                             config.shape_fade_distance_scale);
}

[[nodiscard]] inline float ocean_cascade_domain_high_k(const OceanConfig& config,
                                                       std::uint32_t cascade) {
    const OceanCascadeConfig& cascade_config = ocean_cascade(config, cascade);
    const std::uint32_t map_size = ocean_cascade_map_size(config, cascade);
    if (map_size == 0U || cascade_config.tile_length <= 0.0F) {
        return 0.0F;
    }
    return 2.0F * kOceanPi * static_cast<float>(map_size) /
           (cascade_config.tile_length * kOceanCascadeSmallestWaveMultiplier);
}

[[nodiscard]] inline OceanCascadeDomain ocean_cascade_domain(const OceanConfig& config,
                                                             std::uint32_t cascade) {
    const OceanCascadeConfig& cascade_config = ocean_cascade(config, cascade);
    if (ocean_cascade_map_size(config, cascade) == 0U || cascade_config.tile_length <= 0.0F) {
        return {};
    }

    const float min_waves_per_domain = cascade_config.domain_min_waves;
    if (min_waves_per_domain <= 0.0F) {
        return {};
    }

    const float low_k = 2.0F * kOceanPi * min_waves_per_domain / cascade_config.tile_length;
    const float high_k = ocean_cascade_domain_high_k(config, cascade);
    if (low_k < 0.0F || high_k <= low_k) {
        return {.low_k = low_k, .high_k = high_k};
    }

    return {
        .low_k = low_k,
        .high_k = high_k,
        .low_wavelength = 2.0F * kOceanPi / high_k,
        .high_wavelength = 2.0F * kOceanPi / low_k,
        .active = true,
    };
}

inline void validate_ocean_config(const OceanConfig& config) {
    static_cast<void>(ocean_mesh_vertex_count(config));
    if (config.mesh_lod_levels < kOceanMinMeshLodLevels ||
        config.mesh_lod_levels > kOceanMaxMeshLodLevels) {
        throw std::runtime_error("ocean mesh LOD levels out of supported range");
    }
    if (!ocean_is_supported_map_size(config.map_size) ||
        !ocean_is_power_of_two(config.map_size)) {
        throw std::runtime_error("ocean map size must be 128, 256, 512, or 1024");
    }
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        const std::uint32_t map_size = ocean_cascade_map_size(config, cascade);
        if (!ocean_is_supported_map_size(map_size) || !ocean_is_power_of_two(map_size)) {
            throw std::runtime_error(
                "ocean cascade map size must be inherited, 128, 256, 512, or 1024");
        }
        if (config.cascade_update_intervals[cascade] == 0U) {
            throw std::runtime_error("ocean cascade update interval must be at least one frame");
        }
    }
    if (config.mesh_extent <= 0.0F || config.depth <= 0.0F ||
        config.horizon_extent_margin <= 0.0F || config.horizon_target_near_cell_m <= 0.0F ||
        config.planet_radius_scale <= 0.0F ||
        config.curvature_start_ratio < 0.0F || config.curvature_end_ratio <= 0.0F ||
        config.curvature_strength < 0.0F || config.curvature_strength > 1.0F ||
        config.curvature_start_ratio >= config.curvature_end_ratio) {
        throw std::runtime_error("ocean mesh, horizon, curvature, and depth controls are invalid");
    }
    if (config.normal_strength < 0.0F || config.roughness < 0.0F || config.roughness > 1.0F ||
        config.foam_density < 0.0F || config.foam_sharpness < 0.0F ||
        config.foam_sharpness > 1.0F || config.surface_shape_strength < 0.0F ||
        config.surface_foam_strength < 0.0F || config.foam_history_strength < 0.0F ||
        config.atmosphere_material_strength < 0.0F ||
        config.atmosphere_material_strength > 1.0F ||
        config.atmosphere_sky_strength < 0.0F || config.atmosphere_sky_strength > 1.0F ||
        config.atmosphere_reflection_strength < 0.0F ||
        config.atmosphere_reflection_strength > 1.0F ||
        config.atmosphere_light_strength < 0.0F || config.atmosphere_light_strength > 1.0F ||
        config.foam_lighting_strength < 0.0F || config.foam_lighting_strength > 1.0F ||
        config.self_shadow_strength < 0.0F || config.self_shadow_strength > 1.0F ||
        config.self_shadow_distance <= 0.0F || config.self_shadow_bias < 0.0F ||
        config.self_shadow_steps == 0U || config.self_shadow_steps > 24U ||
        config.terrain_foam_strength < 0.0F || config.shape_fade_distance_scale <= 0.0F ||
        config.normal_fade_distance_scale <= 0.0F || config.foam_fade_distance_scale <= 0.0F) {
        throw std::runtime_error("ocean shading controls are out of range");
    }
    for (const OceanCascadeConfig& cascade : config.cascades) {
        if (cascade.tile_length <= 0.0F || cascade.wind_speed <= 0.0F ||
            cascade.fetch_length_km <= 0.0F) {
            throw std::runtime_error("ocean cascade wave dimensions must be positive");
        }
        if (cascade.displacement_scale < 0.0F || cascade.normal_scale < 0.0F ||
            cascade.swell < 0.0F || cascade.spread < 0.0F || cascade.spread > 1.0F ||
            cascade.detail < 0.0F || cascade.detail > 1.0F || cascade.whitecap < 0.0F ||
            cascade.foam_amount < 0.0F || cascade.domain_min_waves < 0.0F) {
            throw std::runtime_error("ocean cascade controls are out of range");
        }
    }
}

[[nodiscard]] inline OceanConfig ocean_config_from_run_config(const RunConfig& config) {
    OceanConfig ocean;
    if (config.ocean.map_size != 0U) {
        ocean.map_size = config.ocean.map_size;
    }
    if (config.ocean.spectral_domains >= 0) {
        ocean.spectral_domains_enabled = config.ocean.spectral_domains != 0;
    }
    if (config.ocean.terrain_fields >= 0) {
        ocean.terrain_fields_enabled = config.ocean.terrain_fields != 0;
    }
    ocean.field_precision = ocean_field_precision_from_name(config.ocean.field_precision);
    ocean.surface_mode = ocean_surface_mode_from_name(config.ocean.surface_mode);
    if (run_config_float_is_set(config.ocean.planet_radius_scale)) {
        ocean.planet_radius_scale = config.ocean.planet_radius_scale;
    }
    if (run_config_float_is_set(config.ocean.curvature_start_ratio)) {
        ocean.curvature_start_ratio = config.ocean.curvature_start_ratio;
    }
    if (run_config_float_is_set(config.ocean.curvature_end_ratio)) {
        ocean.curvature_end_ratio = config.ocean.curvature_end_ratio;
    }
    if (run_config_float_is_set(config.ocean.curvature_strength)) {
        ocean.curvature_strength = config.ocean.curvature_strength;
    }
    ocean.render_view = ocean_render_view_from_name(config.debug_view);
    ocean.exposure = config.pbr.exposure;
    validate_ocean_config(ocean);
    return ocean;
}

} // namespace cubey::projects::ocean
