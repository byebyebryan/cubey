#pragma once

#include <cubey/core/run_config.h>

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
    Lod = 5,
};

inline constexpr std::array<OceanRenderView, 6> kOceanRenderViews{
    OceanRenderView::Final,  OceanRenderView::Height, OceanRenderView::Displacement,
    OceanRenderView::Normal, OceanRenderView::Foam,   OceanRenderView::Lod,
};

inline constexpr std::array<std::uint32_t, 4> kOceanSupportedMapSizes{128U, 256U, 512U, 1024U};
inline constexpr std::uint32_t kOceanDefaultMapSize = 1024U;
inline constexpr std::uint32_t kOceanCascadeCount = 3U;
inline constexpr std::uint32_t kOceanSpectrumFieldCount = 4U;
inline constexpr std::uint32_t kOceanMinMeshCells = 32U;
inline constexpr std::uint32_t kOceanMaxMeshCells = 512U;
inline constexpr std::uint32_t kOceanMinMeshLodLevels = 1U;
inline constexpr std::uint32_t kOceanMaxMeshLodLevels = 6U;

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
    OceanRenderView render_view = OceanRenderView::Final;
    std::array<OceanCascadeConfig, kOceanCascadeCount> cascades{
        OceanCascadeConfig{},
        OceanCascadeConfig{
            .tile_length = 57.0F,
            .displacement_scale = 0.75F,
            .normal_scale = 1.0F,
            .wind_speed = 5.0F,
            .wind_direction_degrees = 15.0F,
            .fetch_length_km = 150.0F,
            .swell = 0.8F,
            .spread = 0.4F,
            .detail = 1.0F,
            .whitecap = 0.5F,
            .foam_amount = 0.0F,
            .seed_x = -2713,
            .seed_y = 8128,
            .time_offset = 123.14159F,
        },
        OceanCascadeConfig{
            .tile_length = 16.0F,
            .displacement_scale = 0.0F,
            .normal_scale = 0.25F,
            .wind_speed = 20.0F,
            .wind_direction_degrees = 20.0F,
            .fetch_length_km = 550.0F,
            .swell = 0.8F,
            .spread = 0.4F,
            .detail = 1.0F,
            .whitecap = 0.25F,
            .foam_amount = 3.0F,
            .seed_x = 6619,
            .seed_y = -3544,
            .time_offset = 126.28318F,
        },
    };

    friend bool operator==(const OceanConfig&, const OceanConfig&) = default;
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
    case OceanRenderView::Lod:
        return "lod";
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
    if (config.mesh_extent <= 0.0F || config.depth <= 0.0F) {
        throw std::runtime_error("ocean mesh extent and depth must be positive");
    }
    if (config.normal_strength < 0.0F || config.roughness < 0.0F || config.roughness > 1.0F) {
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
    ocean.render_view = ocean_render_view_from_name(config.debug_view);
    ocean.exposure = config.pbr.exposure;
    validate_ocean_config(ocean);
    return ocean;
}

} // namespace cubey::projects::ocean
