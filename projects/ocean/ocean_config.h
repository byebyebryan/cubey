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
    Detail = 5,
    Reflection = 6,
    Refraction = 7,
    Spectrum = 8,
    Wireframe = 9,
    SceneDepth = 10,
    Thickness = 11,
    Transmittance = 12,
    RefractionOffset = 13,
};

inline constexpr std::array<OceanRenderView, 14> kOceanRenderViews{
    OceanRenderView::Final,         OceanRenderView::Height,
    OceanRenderView::Displacement,  OceanRenderView::Normal,
    OceanRenderView::Foam,          OceanRenderView::Detail,
    OceanRenderView::Reflection,    OceanRenderView::Refraction,
    OceanRenderView::Spectrum,      OceanRenderView::Wireframe,
    OceanRenderView::SceneDepth,    OceanRenderView::Thickness,
    OceanRenderView::Transmittance, OceanRenderView::RefractionOffset,
};

inline constexpr std::uint32_t kOceanMinMeshCells = 32;
inline constexpr std::uint32_t kOceanMaxMeshCells = 512;
inline constexpr std::uint32_t kOceanMinMeshLodLevels = 1;
inline constexpr std::uint32_t kOceanMaxMeshLodLevels = 6;
inline constexpr std::uint32_t kOceanMinSpectrumResolution = 64;
inline constexpr std::uint32_t kOceanMaxSpectrumResolution = 512;
inline constexpr std::uint32_t kOceanCascadeCount = 3;
inline constexpr std::uint32_t kOceanSpectrumFieldCount = 3;

struct OceanConfig {
    std::uint32_t mesh_cells = 512;
    std::uint32_t mesh_lod_levels = 5;
    float mesh_extent = 5600.0F;
    float horizon_fog = 0.50F;

    float wind_direction_degrees = -36.0F;
    float sea_state = 1.75F;
    float animation_speed = 1.0F;
    float wave_amplitude = 2.55F;
    float swell_scale = 1.15F;
    float chop = 1.75F;
    float normal_strength = 0.48F;
    float detail_chop = 1.05F;
    float detail_spread = 0.30F;
    float detail_geometry = 0.55F;
    float crest_sharpness = 0.70F;

    float foam_amount = 0.42F;
    float foam_threshold = 0.72F;
    float foam_breakup = 0.55F;
    float absorption = 0.040F;
    float refraction_pixels = 10.0F;
    float water_opacity = 0.34F;
    float scattering_strength = 0.22F;
    bool seafloor_enabled = false;
    float seafloor_depth = 16.0F;
    float seafloor_brightness = 1.30F;
    float seafloor_detail = 1.70F;
    float exposure = 0.0F;

    std::uint32_t spectrum_resolution = 256;
    float spectrum_patch_length_near = 96.0F;
    float spectrum_patch_length_mid = 384.0F;
    float spectrum_patch_length_far = 1536.0F;
    float spectrum_energy = 2.05F;
    float spectrum_fetch = 2.35F;
    float foam_generation = 0.72F;
    float foam_decay = 0.972F;
    float foam_drift = 1.0F;
    std::uint32_t spectrum_seed = 1337;

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
    case OceanRenderView::Displacement:
        return "displacement";
    case OceanRenderView::Normal:
        return "normal";
    case OceanRenderView::Foam:
        return "foam";
    case OceanRenderView::Detail:
        return "detail";
    case OceanRenderView::Reflection:
        return "reflection";
    case OceanRenderView::Refraction:
        return "refraction";
    case OceanRenderView::Spectrum:
        return "spectrum";
    case OceanRenderView::Wireframe:
        return "wireframe";
    case OceanRenderView::SceneDepth:
        return "scene-depth";
    case OceanRenderView::Thickness:
        return "thickness";
    case OceanRenderView::Transmittance:
        return "transmittance";
    case OceanRenderView::RefractionOffset:
        return "refraction-offset";
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

[[nodiscard]] inline bool ocean_is_power_of_two(std::uint32_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

[[nodiscard]] inline float ocean_cascade_patch_length(const OceanConfig& config,
                                                      std::uint32_t cascade) {
    switch (cascade) {
    case 0:
        return config.spectrum_patch_length_near;
    case 1:
        return config.spectrum_patch_length_mid;
    case 2:
        return config.spectrum_patch_length_far;
    default:
        throw std::runtime_error("ocean cascade index out of range");
    }
}

inline void validate_ocean_config(const OceanConfig& config) {
    static_cast<void>(ocean_mesh_vertex_count(config));
    if (config.mesh_lod_levels < kOceanMinMeshLodLevels ||
        config.mesh_lod_levels > kOceanMaxMeshLodLevels) {
        throw std::runtime_error("ocean mesh LOD levels out of supported range");
    }
    if (config.spectrum_resolution < kOceanMinSpectrumResolution ||
        config.spectrum_resolution > kOceanMaxSpectrumResolution ||
        !ocean_is_power_of_two(config.spectrum_resolution)) {
        throw std::runtime_error("ocean spectrum resolution must be a supported power of two");
    }
    if (config.spectrum_patch_length_near <= 0.0F || config.spectrum_patch_length_mid <= 0.0F ||
        config.spectrum_patch_length_far <= 0.0F) {
        throw std::runtime_error("ocean spectrum patch lengths must be positive");
    }
    if (config.spectrum_patch_length_near >= config.spectrum_patch_length_mid ||
        config.spectrum_patch_length_mid >= config.spectrum_patch_length_far) {
        throw std::runtime_error("ocean spectrum patch lengths must be ordered near < mid < far");
    }
    if (config.spectrum_energy < 0.0F) {
        throw std::runtime_error("ocean spectrum energy must be nonnegative");
    }
    if (config.sea_state <= 0.0F) {
        throw std::runtime_error("ocean sea state must be positive");
    }
    if (config.animation_speed < 0.0F) {
        throw std::runtime_error("ocean animation speed must be nonnegative");
    }
    if (config.detail_chop < 0.0F) {
        throw std::runtime_error("ocean detail chop must be nonnegative");
    }
    if (config.detail_spread < 0.0F) {
        throw std::runtime_error("ocean detail spread must be nonnegative");
    }
    if (config.detail_geometry < 0.0F) {
        throw std::runtime_error("ocean detail geometry must be nonnegative");
    }
    if (config.crest_sharpness < 0.0F) {
        throw std::runtime_error("ocean crest sharpness must be nonnegative");
    }
    if (config.spectrum_fetch <= 0.0F) {
        throw std::runtime_error("ocean spectrum fetch must be positive");
    }
    if (config.foam_generation < 0.0F) {
        throw std::runtime_error("ocean foam generation must be nonnegative");
    }
    if (config.foam_decay < 0.0F || config.foam_decay > 1.0F) {
        throw std::runtime_error("ocean foam decay must be in [0, 1]");
    }
    if (config.foam_breakup < 0.0F) {
        throw std::runtime_error("ocean foam breakup must be nonnegative");
    }
    if (config.foam_drift < 0.0F) {
        throw std::runtime_error("ocean foam drift must be nonnegative");
    }
    if (config.absorption < 0.0F) {
        throw std::runtime_error("ocean absorption must be nonnegative");
    }
    if (config.refraction_pixels < 0.0F) {
        throw std::runtime_error("ocean refraction pixel offset must be nonnegative");
    }
    if (config.water_opacity < 0.0F || config.water_opacity > 1.0F) {
        throw std::runtime_error("ocean water opacity must be in [0, 1]");
    }
    if (config.scattering_strength < 0.0F) {
        throw std::runtime_error("ocean scattering strength must be nonnegative");
    }
    if (config.seafloor_depth <= 0.0F) {
        throw std::runtime_error("ocean seafloor depth must be positive");
    }
    if (config.seafloor_brightness < 0.0F) {
        throw std::runtime_error("ocean seafloor brightness must be nonnegative");
    }
    if (config.seafloor_detail < 0.0F) {
        throw std::runtime_error("ocean seafloor detail must be nonnegative");
    }
}

} // namespace cubey::projects::ocean
