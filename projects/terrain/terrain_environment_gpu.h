#pragma once

#include <cubey/render/atmosphere_environment.h>

#include <array>

namespace cubey::projects::terrain {

struct TerrainEnvironmentGpuParameters {
    cubey::render::AtmosphereEnvironmentFrameUniforms atmosphere{};
    std::array<cubey::math::Vec4, 9> diffuse_irradiance_sh{};
    cubey::math::Vec4 primary_light_direction_intensity{};
    cubey::math::Vec4 primary_light_color_angular_radius{};
};

static_assert(sizeof(TerrainEnvironmentGpuParameters) == sizeof(cubey::math::Vec4) * 28U);

[[nodiscard]] TerrainEnvironmentGpuParameters terrain_environment_gpu_parameters(
    const cubey::render::AtmosphereEnvironmentFrameUniforms& atmosphere,
    const cubey::render::AtmosphereEnvironmentLighting& lighting);

} // namespace cubey::projects::terrain
