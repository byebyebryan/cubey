#pragma once

#include <cubey/core/run_config.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cubey::projects::ocean_exp {

enum class OceanRenderView : std::uint32_t {
    Final = 0,
    Height = 1,
    Displacement = 2,
    Normal = 3,
    Foam = 4,
    FoamSource = 5,
    FoamHistory = 6,
    FoamMacro = 7,
    FoamCrest = 8,
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
};

inline constexpr std::array<OceanRenderView, 21> kOceanRenderViews{
    OceanRenderView::Final,        OceanRenderView::Height,       OceanRenderView::Displacement,
    OceanRenderView::Normal,       OceanRenderView::Foam,         OceanRenderView::FoamSource,
    OceanRenderView::FoamHistory,  OceanRenderView::FoamMacro,    OceanRenderView::FoamCrest,
    OceanRenderView::FoamDetail,   OceanRenderView::Lod,          OceanRenderView::SkyRadiance,
    OceanRenderView::Reflection,   OceanRenderView::DirectLight,  OceanRenderView::AmbientLight,
    OceanRenderView::Exposure,     OceanRenderView::FoamRaw,      OceanRenderView::FoamLit,
    OceanRenderView::TerrainDepth, OceanRenderView::TerrainShore, OceanRenderView::TerrainSlope,
};

inline constexpr std::array<std::uint32_t, 4> kOceanSupportedMapSizes{128U, 256U, 512U, 1024U};
inline constexpr std::uint32_t kOceanDefaultMapSize = 1024U;
inline constexpr std::uint32_t kOceanMacroCascadeCount = 2U;
inline constexpr std::uint32_t kOceanReferenceCascadeCount = 3U;
inline constexpr std::uint32_t kOceanCascadeCount =
    kOceanMacroCascadeCount + kOceanReferenceCascadeCount;
inline constexpr std::uint32_t kOceanSpectrumFieldCount = 4U;
inline constexpr std::uint32_t kOceanMinMeshCells = 32U;
inline constexpr std::uint32_t kOceanMaxMeshCells = 512U;
inline constexpr std::uint32_t kOceanMinMeshLodLevels = 1U;
inline constexpr std::uint32_t kOceanMaxMeshLodLevels = 6U;
inline constexpr float kOceanPi = 3.14159265358979323846F;
inline constexpr std::array<float, kOceanCascadeCount> kOceanCascadeMinWavesPerDomainByCascade{
    3.0F, 2.0F, 0.0F, 0.0F, 3.0F,
};
inline constexpr float kOceanCascadeSmallestWaveMultiplier = 4.0F;

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
    bool spectral_domains_enabled = true;
    bool terrain_fields_enabled = false;
    OceanRenderView render_view = OceanRenderView::Final;
    std::array<OceanCascadeConfig, kOceanCascadeCount> cascades{
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
            .seed_x = 9311,
            .seed_y = -1733,
            .time_offset = 117.0F,
        },
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
            .seed_x = -2713,
            .seed_y = 8128,
            .time_offset = 123.14159F,
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
    case OceanRenderView::FoamMacro:
        return "foam-macro";
    case OceanRenderView::FoamCrest:
        return "foam-crest";
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
    }
    return "final";
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

[[nodiscard]] inline float ocean_cascade_domain_high_k(const OceanConfig& config,
                                                       std::uint32_t cascade) {
    const OceanCascadeConfig& cascade_config = ocean_cascade(config, cascade);
    if (config.map_size == 0U || cascade_config.tile_length <= 0.0F) {
        return 0.0F;
    }
    return 2.0F * kOceanPi * static_cast<float>(config.map_size) /
           (cascade_config.tile_length * kOceanCascadeSmallestWaveMultiplier);
}

[[nodiscard]] inline float ocean_cascade_min_waves_per_domain(std::uint32_t cascade) {
    if (cascade >= kOceanCascadeCount) {
        throw std::runtime_error("ocean cascade index out of range");
    }
    return kOceanCascadeMinWavesPerDomainByCascade[cascade];
}

[[nodiscard]] inline OceanCascadeDomain ocean_cascade_domain(const OceanConfig& config,
                                                             std::uint32_t cascade) {
    const OceanCascadeConfig& cascade_config = ocean_cascade(config, cascade);
    if (config.map_size == 0U || cascade_config.tile_length <= 0.0F) {
        return {};
    }

    const float min_waves_per_domain = ocean_cascade_min_waves_per_domain(cascade);
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
    if (!ocean_is_supported_map_size(config.map_size) || !ocean_is_power_of_two(config.map_size)) {
        throw std::runtime_error("ocean map size must be 128, 256, 512, or 1024");
    }
    if (config.mesh_extent <= 0.0F || config.depth <= 0.0F) {
        throw std::runtime_error("ocean mesh extent and depth must be positive");
    }
    if (config.normal_strength < 0.0F || config.roughness < 0.0F || config.roughness > 1.0F ||
        config.foam_density < 0.0F || config.foam_sharpness < 0.0F ||
        config.foam_sharpness > 1.0F) {
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
            cascade.foam_amount < 0.0F) {
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
    ocean.render_view = ocean_render_view_from_name(config.debug_view);
    ocean.exposure = config.pbr.exposure;
    validate_ocean_config(ocean);
    return ocean;
}

} // namespace cubey::projects::ocean_exp
