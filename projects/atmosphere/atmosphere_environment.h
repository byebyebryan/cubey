#pragma once

#include "atmosphere_config.h"

#include <cubey/render/view_ray_basis_3d.h>

namespace cubey::projects::atmosphere {

struct AtmosphereFrameUniforms {
    cubey::math::Vec4 camera_right_aspect;
    cubey::math::Vec4 camera_up_tan_half_fovy;
    cubey::math::Vec4 camera_forward_debug_view;
    cubey::math::Vec4 radii_ground;
    cubey::math::Vec4 rayleigh;
    cubey::math::Vec4 mie;
    cubey::math::Vec4 ozone;
    cubey::math::Vec4 sun_direction_radius;
    cubey::math::Vec4 display_transform;
    cubey::math::Vec4 atmosphere_options;
    cubey::math::Vec4 night_options;
    cubey::math::Vec4 celestial_options;
    cubey::math::Vec4 moon_direction_radius;
    cubey::math::Vec4 moon_options;
    cubey::math::Vec4 moon_phase_options;
    cubey::math::Vec4 milky_way_options;
};

static_assert(sizeof(AtmosphereFrameUniforms) == sizeof(float) * 64U);

struct AtmosphereFrameUniformInputs {
    cubey::render::ViewRayBasis3D view_rays{};
    AtmosphereRenderView render_view = AtmosphereRenderView::Final;
    cubey::math::Vec4 display_transform{0.0F, 0.0F, 0.0F, 0.0F};
};

[[nodiscard]] cubey::math::Vec3 atmosphere_sun_direction(const AtmosphereConfig& config);
[[nodiscard]] AtmosphereFrameUniforms
atmosphere_frame_uniforms(const AtmosphereConfig& config,
                          const AtmosphereFrameUniformInputs& inputs);

} // namespace cubey::projects::atmosphere
