#pragma once

#include "terrain_shadow.h"

#include <cubey/render/atmosphere_environment.h>

#include <array>

namespace cubey::projects::terrain {

struct TerrainEnvironmentGpuParameters {
    cubey::render::AtmosphereEnvironmentFrameUniforms atmosphere{};
    std::array<cubey::math::Vec4, 9> diffuse_irradiance_sh{};
    cubey::math::Vec4 primary_light_direction_intensity{};
    cubey::math::Vec4 primary_light_color_angular_radius{};
    cubey::math::Mat4 light_view_projection{1.0F};
    cubey::math::Vec4 shadow_options{};
};

static_assert(sizeof(TerrainEnvironmentGpuParameters) == sizeof(cubey::math::Vec4) * 33U);

[[nodiscard]] TerrainEnvironmentGpuParameters terrain_environment_gpu_parameters(
    const cubey::render::AtmosphereEnvironmentFrameUniforms& atmosphere,
    const cubey::render::AtmosphereEnvironmentLighting& lighting,
    const TerrainShadowProjection& shadow, bool shadows_enabled);

} // namespace cubey::projects::terrain
