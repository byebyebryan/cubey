#include "terrain_environment_gpu.h"

namespace cubey::projects::terrain {

TerrainEnvironmentGpuParameters terrain_environment_gpu_parameters(
    const cubey::render::AtmosphereEnvironmentFrameUniforms& atmosphere,
    const cubey::render::AtmosphereEnvironmentLighting& lighting) {
    TerrainEnvironmentGpuParameters result{
        .atmosphere = atmosphere,
        .primary_light_direction_intensity =
            {lighting.primary_light_direction.x, lighting.primary_light_direction.y,
             lighting.primary_light_direction.z, lighting.primary_light_intensity},
    };
    for (std::size_t index = 0U; index < result.diffuse_irradiance_sh.size(); ++index) {
        const cubey::math::Vec3 coefficient = lighting.diffuse_irradiance_sh[index];
        result.diffuse_irradiance_sh[index] =
            {coefficient.x, coefficient.y, coefficient.z, 0.0F};
    }
    const bool sun_is_primary = lighting.sun_intensity >= lighting.moon_intensity;
    const float angular_radius = sun_is_primary ? atmosphere.sun_direction_radius.w
                                                : atmosphere.moon_direction_radius.w;
    result.primary_light_color_angular_radius =
        {lighting.primary_light_color.x, lighting.primary_light_color.y,
         lighting.primary_light_color.z, angular_radius};
    return result;
}

} // namespace cubey::projects::terrain
