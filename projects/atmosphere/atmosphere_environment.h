#pragma once

#include "atmosphere_config.h"

#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/view_ray_basis_3d.h>

namespace cubey::projects::atmosphere {

using AtmosphereFrameUniforms = cubey::render::AtmosphereEnvironmentFrameUniforms;
static_assert(sizeof(AtmosphereFrameUniforms) == sizeof(float) * 64U);

struct AtmosphereFrameUniformInputs {
    cubey::render::ViewRayBasis3D view_rays{};
    AtmosphereRenderView render_view = AtmosphereRenderView::Final;
};

[[nodiscard]] cubey::render::AtmosphereEnvironmentRenderView
atmosphere_environment_render_view(AtmosphereRenderView view);
[[nodiscard]] cubey::render::AtmosphereEnvironmentConfig
atmosphere_environment_config(const AtmosphereConfig& config);
[[nodiscard]] cubey::math::Vec3 atmosphere_sun_direction(const AtmosphereConfig& config);
[[nodiscard]] AtmosphereFrameUniforms
atmosphere_frame_uniforms(const AtmosphereConfig& config,
                          const AtmosphereFrameUniformInputs& inputs);

} // namespace cubey::projects::atmosphere
