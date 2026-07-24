#version 450
#extension GL_GOOGLE_include_directive : require

#include "water_3d_surface_common.glsl"

WATER3D_SURFACE_PARAMS;

#include "water_3d_surface_helpers.glsl"

layout(set = 0, binding = 0) uniform samplerCube environment_cube;

layout(location = 0) in vec2 frag_uv;
layout(location = 0) out vec4 out_color;

vec3 rotate_environment_direction(vec3 direction) {
    float c = surface_params.environment_options.x;
    float s = surface_params.environment_options.y;
    return normalize(vec3(
        (c * direction.x) + (s * direction.z),
        direction.y,
        (-s * direction.x) + (c * direction.z)
    ));
}

vec3 sample_environment(vec3 direction) {
    return textureLod(environment_cube, rotate_environment_direction(direction), 0.0).rgb *
           surface_params.environment_options.z;
}

vec3 scene_view_ray(vec2 uv) {
    vec2 screen = vec2((uv.x * 2.0) - 1.0, ((1.0 - uv.y) * 2.0) - 1.0);
    float tan_half_fovy = surface_params.camera_right_tan.w;
    float aspect = surface_params.camera_up_aspect.w;
    return normalize(water_surface_camera_forward() +
                     water_surface_camera_right() * (screen.x * tan_half_fovy * aspect) +
                     water_surface_camera_up() * (screen.y * tan_half_fovy));
}

void main() {
    vec3 ray = scene_view_ray(frag_uv);
    out_color = vec4(sample_environment(ray), 1.0);
    gl_FragDepth = 1.0;
}
