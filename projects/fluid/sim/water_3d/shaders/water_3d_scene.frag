#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"
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

float clip_depth(vec3 world_position) {
    vec4 clip = surface_params.view_projection * vec4(world_position, 1.0);
    if (clip.w <= 0.0) {
        return 1.0;
    }
    return clamp(clip.z / clip.w, 0.0, 1.0);
}

vec3 ground_color(vec3 world_position) {
    vec2 scaled = world_position.xz * 7.0;
    vec2 cell = floor(scaled);
    float checker = mod(cell.x + cell.y, 2.0);
    vec2 cell_uv = fract(scaled);
    float grid_x = 1.0 - smoothstep(0.015, 0.040, min(cell_uv.x, 1.0 - cell_uv.x));
    float grid_z = 1.0 - smoothstep(0.015, 0.040, min(cell_uv.y, 1.0 - cell_uv.y));
    float grid = max(grid_x, grid_z);

    vec3 base_a = cubey_srgb_to_linear(vec3(0.23, 0.25, 0.25));
    vec3 base_b = cubey_srgb_to_linear(vec3(0.30, 0.32, 0.31));
    vec3 line = cubey_srgb_to_linear(vec3(0.48, 0.52, 0.54));
    vec3 base = mix(base_a, base_b, checker);
    vec3 color = mix(base, line, grid * 0.55);

    vec3 light_dir = normalize(vec3(-0.42, 0.78, 0.46));
    float diffuse = max(dot(vec3(0.0, 1.0, 0.0), light_dir), 0.0);
    return color * (0.42 + diffuse * 0.58);
}

void main() {
    vec3 camera_position = water_surface_camera_position();
    vec3 ray = scene_view_ray(frag_uv);
    vec3 color = sample_environment(ray);
    float depth = 1.0;

    const float ground_y = -0.025;
    if (ray.y < -0.0001) {
        float t = (ground_y - camera_position.y) / ray.y;
        if (t > 0.0) {
            vec3 world_position = camera_position + ray * t;
            if (world_position.x >= -0.35 && world_position.x <= 1.35 &&
                world_position.z >= -0.35 && world_position.z <= 1.35) {
                color = ground_color(world_position);
                depth = clip_depth(world_position);
            }
        }
    }

    out_color = vec4(color, 1.0);
    gl_FragDepth = depth;
}
