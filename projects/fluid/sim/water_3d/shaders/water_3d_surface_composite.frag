#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"
#include "water_3d_surface_common.glsl"

WATER3D_SURFACE_PARAMS;

#include "water_3d_surface_helpers.glsl"

layout(set = 0, binding = 0) uniform sampler2D surface_texture;

layout(location = 0) in vec2 frag_uv;
layout(location = 0) out vec4 out_color;

vec3 environment_linear(vec3 direction) {
    vec3 ray = normalize(direction);
    float horizon = clamp(ray.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 sky = cubey_srgb_to_linear(mix(vec3(0.52, 0.66, 0.82), vec3(0.90, 0.95, 1.0),
                                        smoothstep(0.15, 1.0, horizon)));
    vec3 ground = cubey_srgb_to_linear(mix(vec3(0.030, 0.040, 0.050), vec3(0.20, 0.24, 0.25),
                                           smoothstep(0.0, 0.65, horizon)));
    vec3 color = mix(ground, sky, smoothstep(0.35, 0.72, horizon));
    float sun = pow(max(dot(ray, normalize(vec3(-0.35, 0.72, 0.42))), 0.0), 380.0);
    return color + vec3(3.0, 2.65, 2.25) * sun;
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
    vec3 camera_ray = water_surface_view_ray(frag_uv);
    vec3 background = environment_linear(camera_ray);
    if (!water_surface_has_depth(surface.x)) {
        out_color = vec4(background, 1.0);
        return;
    }

    float depth = surface.x;
    float thickness = max(surface.y, 0.0);
    vec3 position = water_surface_world_position(frag_uv, depth);
    vec3 normal = reconstruct_normal(frag_uv, depth);
    uint view = water_surface_render_view();

    if (view == WATER3D_SURFACE_VIEW_DEPTH) {
        float depth_value = debug_depth_value(depth);
        out_color = vec4(cubey_srgb_to_linear(vec3(depth_value)), 1.0);
        return;
    }
    if (view == WATER3D_SURFACE_VIEW_THICKNESS) {
        float thickness_value = debug_thickness_value(thickness);
        vec3 debug_color = mix(vec3(0.025, 0.060, 0.085), vec3(0.42, 0.82, 1.0),
                               thickness_value);
        out_color = vec4(cubey_srgb_to_linear(debug_color), 1.0);
        return;
    }
    if (view == WATER3D_SURFACE_VIEW_NORMALS) {
        out_color = vec4(normal * 0.5 + 0.5, 1.0);
        return;
    }

    vec3 view_dir = normalize(water_surface_camera_position() - position);
    vec3 reflect_dir = reflect(-view_dir, normal);
    vec3 refract_dir = normalize(mix(camera_ray, refract(camera_ray, normal, 1.0 / 1.333),
                                     clamp(surface_params.surface_options.w * 10.0, 0.0, 1.0)));
    float ndotv = clamp(dot(normal, view_dir), 0.0, 1.0);
    float fresnel = 0.02037 + (1.0 - 0.02037) * pow(1.0 - ndotv, 5.0);

    vec3 absorption = vec3(1.85, 0.42, 0.08) * max(0.0, surface_params.surface_options.z);
    vec3 transmittance = exp(-absorption * max(0.0, thickness));
    vec3 refracted = environment_linear(refract_dir) * transmittance;
    vec3 reflected = environment_linear(reflect_dir);
    vec3 light_dir = normalize(vec3(-0.28, 0.80, 0.52));
    vec3 half_dir = normalize(light_dir + view_dir);
    float specular = pow(max(dot(normal, half_dir), 0.0), 96.0) * 0.22;
    vec3 water_tint = cubey_srgb_to_linear(vec3(0.025, 0.22, 0.30)) * (1.0 - transmittance);
    vec3 color = mix(refracted + water_tint, reflected, fresnel) + vec3(specular);

    out_color = vec4(color, 1.0);
}
