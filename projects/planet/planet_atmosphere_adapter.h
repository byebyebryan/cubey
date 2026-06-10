#pragma once

#include "planet_celestial.h"

#include <cubey/render/atmosphere_environment.h>

namespace cubey::projects::planet {

[[nodiscard]] cubey::render::AtmosphereEnvironmentConfig
planet_atmosphere_environment_config(const PlanetAtmosphereInputs& inputs);

struct PlanetSharedAtmosphereFrameInputs {
    cubey::render::ViewRayBasis3D view_rays{};
    cubey::render::AtmosphereEnvironmentRenderView render_view =
        cubey::render::AtmosphereEnvironmentRenderView::Final;
};

[[nodiscard]] cubey::render::AtmosphereEnvironmentFrameUniforms
planet_shared_atmosphere_frame_uniforms(const PlanetAtmosphereInputs& inputs,
                                        const PlanetSharedAtmosphereFrameInputs& frame_inputs);

} // namespace cubey::projects::planet
