#pragma once

#include <cubey/engine/ocean_surface_runtime.h>

namespace cubey::ocean_surface_detail {

struct OceanSurfacePushConstants {
    math::Mat4 view_projection;
    math::Vec4 camera_time;
    math::Vec4 mesh_options;
    math::Vec4 patch_bounds;
    math::Vec4 debug_options;
    math::Vec4 inspection_options;
    math::Vec4 tile_lengths;
    math::Vec4 displacement_scales;
    math::Vec4 normal_scales;
    math::Vec4 cascade4_options;
    math::Vec4 water_color;
    math::Vec4 foam_color;
    math::Vec4 quality_options;
};

struct OceanSpectrumPushConstants {
    math::Vec4 seed_tile;
    math::Vec4 spectrum_options;
    math::Vec4 shape_options;
    math::Vec4 cascade_options;
};

struct OceanModulatePushConstants {
    math::Vec4 tile_depth_time;
    math::Vec4 cascade_options;
};

struct OceanFftPushConstants {
    math::Vec4 fft_options;
    math::Vec4 pass_options;
};

struct OceanUnpackPushConstants {
    math::Vec4 foam_options;
    math::Vec4 cascade_options;
};

static_assert(sizeof(OceanSurfacePushConstants) == sizeof(float) * 64U);
static_assert(sizeof(OceanSpectrumPushConstants) == sizeof(float) * 16U);
static_assert(sizeof(OceanModulatePushConstants) == sizeof(float) * 8U);
static_assert(sizeof(OceanFftPushConstants) == sizeof(float) * 8U);
static_assert(sizeof(OceanUnpackPushConstants) == sizeof(float) * 8U);
[[nodiscard]] inline std::uint32_t field_texture_index(std::uint32_t cascade, std::uint32_t field) {
    return cascade * cubey::render::kOceanSpectrumFieldCount + field;
}


} // namespace cubey::ocean_surface_detail
