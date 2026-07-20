#ifndef CUBEY_TERRAIN_ENVIRONMENT_GLSL
#define CUBEY_TERRAIN_ENVIRONMENT_GLSL

#include "cubey/atmosphere.glsl"

const float ATMOSPHERE_SUN_INTENSITY = 22.0;
const float ATMOSPHERE_MIN_TWILIGHT_SOFTNESS = 0.022;

layout(set = 0, binding = 0, std140) uniform TerrainEnvironmentUniforms {
    vec4 camera_right_aspect;
    vec4 camera_up_tan_half_fovy;
    vec4 camera_forward_debug_view;
    vec4 camera_position_radius;
    vec4 radii_ground;
    vec4 rayleigh;
    vec4 mie;
    vec4 ozone;
    vec4 sun_direction_radius;
    vec4 atmosphere_options;
    vec4 night_options;
    vec4 celestial_options;
    vec4 moon_direction_radius;
    vec4 moon_options;
    vec4 milky_way_options;
    vec4 render_options;
    vec4 celestial_render_options;
    vec4 diffuse_irradiance_sh[9];
    vec4 primary_light_direction_intensity;
    vec4 primary_light_color_angular_radius;
} atmosphere;

#include "cubey/atmosphere/atmosphere_common.glsl"

vec3 terrain_diffuse_irradiance(vec3 direction) {
    vec3 normal = normalize(direction);
    float x = normal.x;
    float y = normal.y;
    float z = normal.z;
    float basis[9] = float[](
        0.282095,
        0.488603 * y,
        0.488603 * z,
        0.488603 * x,
        1.092548 * x * y,
        1.092548 * y * z,
        0.315392 * ((3.0 * z * z) - 1.0),
        1.092548 * x * z,
        0.546274 * ((x * x) - (y * y))
    );
    vec3 irradiance = vec3(0.0);
    for (int index = 0; index < 9; ++index) {
        irradiance += atmosphere.diffuse_irradiance_sh[index].rgb * basis[index];
    }
    return max(irradiance, vec3(0.0));
}

CubeyAtmosphereSample terrain_aerial_perspective(vec3 camera_position_m, vec3 world_position_m) {
    vec3 camera_to_surface_m = world_position_m - camera_position_m;
    float distance_m = length(camera_to_surface_m);
    if (distance_m <= 0.001) {
        return CubeyAtmosphereSample(vec3(0.0), vec3(0.0), vec3(0.0), vec3(1.0),
                                    CubeyAtmosphereOpticalDepth(0.0, 0.0, 0.0), 0.0);
    }
    return integrate_atmosphere(atmosphere.camera_position_radius.xyz,
                                camera_to_surface_m / distance_m, 0.0, distance_m * 0.001,
                                atmosphere_planet_center());
}

#endif // CUBEY_TERRAIN_ENVIRONMENT_GLSL
