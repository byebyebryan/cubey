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
    Normal = 2,
    Foam = 3,
    Reflection = 4,
    Refraction = 5,
    Depth = 6,
};

inline constexpr std::array<OceanRenderView, 7> kOceanRenderViews{
    OceanRenderView::Final,      OceanRenderView::Height, OceanRenderView::Normal,
    OceanRenderView::Foam,       OceanRenderView::Reflection,
    OceanRenderView::Refraction, OceanRenderView::Depth,
};

inline constexpr std::uint32_t kOceanMinMeshCells = 32;
inline constexpr std::uint32_t kOceanMaxMeshCells = 320;

struct OceanConfig {
    std::uint32_t mesh_cells = 220;
    float mesh_extent = 3200.0F;
    float mesh_snap = 16.0F;
    float horizon_fog = 0.78F;

    float wind_direction_degrees = -36.0F;
    float wind_speed = 1.0F;
    float wave_amplitude = 0.85F;
    float swell_scale = 1.0F;
    float chop = 0.9F;
    float normal_strength = 0.88F;

    float foam_amount = 0.34F;
    float foam_threshold = 1.02F;
    float absorption = 0.085F;
    float refraction_strength = 0.055F;
    float exposure = 0.0F;

    float shoreline_influence = 0.0F;
    float disturbance_radius = 42.0F;
    float disturbance_strength = 0.0F;
    OceanRenderView render_view = OceanRenderView::Final;
};

[[nodiscard]] inline const char* ocean_render_view_name(OceanRenderView view) {
    switch (view) {
    case OceanRenderView::Final:
        return "final";
    case OceanRenderView::Height:
        return "height";
    case OceanRenderView::Normal:
        return "normal";
    case OceanRenderView::Foam:
        return "foam";
    case OceanRenderView::Reflection:
        return "reflection";
    case OceanRenderView::Refraction:
        return "refraction";
    case OceanRenderView::Depth:
        return "depth";
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

[[nodiscard]] inline OceanConfig ocean_config_from_run_config(const RunConfig& config) {
    OceanConfig ocean;
    ocean.render_view = ocean_render_view_from_name(config.debug_view);
    ocean.exposure = config.pbr.exposure;
    return ocean;
}

[[nodiscard]] inline std::uint32_t ocean_mesh_vertex_count(const OceanConfig& config) {
    if (config.mesh_cells < kOceanMinMeshCells || config.mesh_cells > kOceanMaxMeshCells) {
        throw std::runtime_error("ocean mesh cells out of supported range");
    }
    return config.mesh_cells * config.mesh_cells * 6U;
}

} // namespace cubey::projects::ocean
