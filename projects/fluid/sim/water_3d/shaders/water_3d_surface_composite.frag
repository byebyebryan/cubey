#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"
#include "cubey/pbr.glsl"
#include "water_3d_surface_common.glsl"

WATER3D_SURFACE_PARAMS;

#include "water_3d_surface_helpers.glsl"

layout(set = 0, binding = 0) uniform sampler2D surface_texture;
layout(set = 0, binding = 1) uniform sampler2D scene_color_texture;
layout(set = 0, binding = 2) uniform sampler2D scene_depth_texture;
layout(set = 0, binding = 3) uniform samplerCube environment_cube;

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

vec3 apply_display_transform(vec3 color) {
    return cubey_pbr_apply_display_transform(color, surface_params.display_transform);
}

float clip_depth(vec3 world_position) {
    vec4 clip = surface_params.view_projection * vec4(world_position, 1.0);
    if (clip.w <= 0.0) {
        return 1.0;
    }
    return clamp(clip.z / clip.w, 0.0, 1.0);
}

float debug_depth_value(float depth) {
    return clamp(1.0 - exp(-depth * 0.42), 0.0, 1.0);
}

float debug_thickness_value(float thickness) {
    return clamp(1.0 - exp(-thickness * 18.0), 0.0, 1.0);
}

vec3 reconstruct_normal(vec2 uv, float center_depth) {
    ivec2 texture_size = textureSize(surface_texture, 0);
    vec2 texel_size = 1.0 / vec2(texture_size);
    float left_depth = texture(surface_texture, uv - vec2(texel_size.x, 0.0)).x;
    float right_depth = texture(surface_texture, uv + vec2(texel_size.x, 0.0)).x;
    float down_depth = texture(surface_texture, uv - vec2(0.0, texel_size.y)).x;
    float up_depth = texture(surface_texture, uv + vec2(0.0, texel_size.y)).x;

    if (!water_surface_has_depth(left_depth)) {
        left_depth = center_depth;
    }
    if (!water_surface_has_depth(right_depth)) {
        right_depth = center_depth;
    }
    if (!water_surface_has_depth(down_depth)) {
        down_depth = center_depth;
    }
    if (!water_surface_has_depth(up_depth)) {
        up_depth = center_depth;
    }

    vec3 p_left = water_surface_world_position(uv - vec2(texel_size.x, 0.0), left_depth);
    vec3 p_right = water_surface_world_position(uv + vec2(texel_size.x, 0.0), right_depth);
    vec3 p_down = water_surface_world_position(uv - vec2(0.0, texel_size.y), down_depth);
    vec3 p_up = water_surface_world_position(uv + vec2(0.0, texel_size.y), up_depth);
    vec3 dpdx = p_right - p_left;
    vec3 dpdy = p_up - p_down;
    vec3 view_dir = normalize(water_surface_camera_position() -
                              water_surface_world_position(uv, center_depth));
    vec3 normal_cross = cross(dpdx, dpdy);
    float normal_length = length(normal_cross);
    vec3 normal = normal_length > 0.00001 ? normal_cross / normal_length : view_dir;
    if (dot(normal, view_dir) < 0.0) {
        normal = -normal;
    }
    return normal;
}

void main() {
    vec4 surface = texture(surface_texture, frag_uv);
    vec3 background = texture(scene_color_texture, frag_uv).rgb;
    if (!water_surface_has_depth(surface.x)) {
        out_color = vec4(apply_display_transform(background), 1.0);
        return;
    }

    float depth = surface.x;
    float thickness = max(surface.y, 0.0);
    vec3 position = water_surface_world_position(frag_uv, depth);
    vec3 normal = reconstruct_normal(frag_uv, depth);
    uint view = water_surface_render_view();

    float water_clip_depth = clip_depth(position);
    float scene_clip_depth = texture(scene_depth_texture, frag_uv).r;
    if (scene_clip_depth < water_clip_depth - 0.0005) {
        out_color = vec4(apply_display_transform(background), 1.0);
        return;
    }

    if (view == WATER3D_SURFACE_VIEW_DEPTH) {
        float depth_value = debug_depth_value(depth);
        out_color = vec4(apply_display_transform(cubey_srgb_to_linear(vec3(depth_value))), 1.0);
        return;
    }
    if (view == WATER3D_SURFACE_VIEW_THICKNESS) {
        float thickness_value = debug_thickness_value(thickness);
        vec3 debug_color = mix(vec3(0.025, 0.060, 0.085), vec3(0.42, 0.82, 1.0),
                               thickness_value);
        out_color = vec4(apply_display_transform(cubey_srgb_to_linear(debug_color)), 1.0);
        return;
    }
    if (view == WATER3D_SURFACE_VIEW_NORMALS) {
        out_color = vec4(apply_display_transform(normal * 0.5 + 0.5), 1.0);
        return;
    }

    vec3 view_dir = normalize(water_surface_camera_position() - position);
    vec3 reflect_dir = reflect(-view_dir, normal);
    float ndotv = clamp(dot(normal, view_dir), 0.0, 1.0);
    float fresnel = 0.02037 + (1.0 - 0.02037) * pow(1.0 - ndotv, 5.0);

    vec3 absorption = vec3(1.85, 0.42, 0.08) * max(0.0, surface_params.surface_options.z);
    vec3 transmittance = exp(-absorption * max(0.0, thickness));
    vec2 normal_screen = vec2(dot(normal, water_surface_camera_right()),
                              dot(normal, water_surface_camera_up()));
    float refraction_amount = surface_params.surface_options.w *
                              clamp(thickness * 0.45, 0.0, 1.0);
    vec2 refract_uv = clamp(frag_uv - normal_screen * refraction_amount, vec2(0.001),
                            vec2(0.999));
    vec3 refracted_scene = texture(scene_color_texture, refract_uv).rgb;
    vec3 refracted = refracted_scene * transmittance;
    vec3 reflected = sample_environment(reflect_dir);
    vec3 light_dir = normalize(vec3(-0.28, 0.80, 0.52));
    vec3 half_dir = normalize(light_dir + view_dir);
    float specular = pow(max(dot(normal, half_dir), 0.0), 96.0) * 0.22;
    vec3 water_tint = cubey_srgb_to_linear(vec3(0.025, 0.22, 0.30)) * (1.0 - transmittance);
    vec3 color = mix(refracted + water_tint, reflected, fresnel) + vec3(specular);

    out_color = vec4(apply_display_transform(color), 1.0);
}
