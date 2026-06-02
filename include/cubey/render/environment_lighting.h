#pragma once

#include <cubey/core/math.h>
#include <cubey/render/atmosphere_environment.h>

#include <algorithm>
#include <array>

namespace cubey::render {

struct EnvironmentLightingUniforms {
    std::array<float, 4> primary_light_direction_intensity{};
    std::array<float, 4> primary_light_color_exposure{};
    std::array<float, 4> ambient_color_intensity{};
    std::array<float, 4> sky_color_options{};
};

static_assert(sizeof(EnvironmentLightingUniforms) == sizeof(float) * 16U);

[[nodiscard]] inline std::array<float, 4> vec3_and_float(math::Vec3 value, float scalar) {
    return {value.x, value.y, value.z, scalar};
}

[[nodiscard]] inline EnvironmentLightingUniforms environment_lighting_uniforms(
    const AtmosphereEnvironmentLighting& lighting, float exposure, float intensity = 1.0F) {
    const math::Vec3 primary_direction =
        glm::length(lighting.primary_light_direction) > 0.0F
            ? glm::normalize(lighting.primary_light_direction)
            : math::Vec3{0.0F, 1.0F, 0.0F};
    const math::Vec3 sky_color = atmosphere_environment_evaluate_sh(
        lighting.diffuse_irradiance_sh, math::Vec3{0.0F, 1.0F, 0.0F});

    return {
        .primary_light_direction_intensity =
            vec3_and_float(primary_direction,
                           std::max(0.0F, lighting.primary_light_intensity * intensity)),
        .primary_light_color_exposure = vec3_and_float(lighting.primary_light_color, exposure),
        .ambient_color_intensity =
            vec3_and_float(lighting.ambient_color,
                           std::max(0.0F, lighting.ambient_intensity * intensity)),
        .sky_color_options = vec3_and_float(sky_color, 1.0F),
    };
}

} // namespace cubey::render
