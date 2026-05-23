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

float readable_depth(float depth, float fallback_depth) {
    return water_surface_has_depth(depth) ? depth : fallback_depth;
}

float foam_noise(vec3 position) {
    float wave_a = sin(dot(position, vec3(37.7, 19.1, 29.3)));
    float wave_b = sin(dot(position, vec3(-23.5, 43.9, 15.7)));
    float wave_c = sin(dot(position, vec3(71.3, -11.9, 53.1)));
    return clamp(0.50 + (wave_a * 0.23) + (wave_b * 0.17) + (wave_c * 0.10), 0.0, 1.0);
}

float foam_mask(vec2 uv, float center_depth, float thickness, vec3 position, vec3 normal) {
    ivec2 texture_size = textureSize(surface_texture, 0);
    vec2 texel_size = 1.0 / vec2(texture_size);
    vec4 left_surface = texture(surface_texture, uv - vec2(texel_size.x, 0.0));
    vec4 right_surface = texture(surface_texture, uv + vec2(texel_size.x, 0.0));
    vec4 down_surface = texture(surface_texture, uv - vec2(0.0, texel_size.y));
    vec4 up_surface = texture(surface_texture, uv + vec2(0.0, texel_size.y));
    bool left_has_depth = water_surface_has_depth(left_surface.x);
    bool right_has_depth = water_surface_has_depth(right_surface.x);
    bool down_has_depth = water_surface_has_depth(down_surface.x);
    bool up_has_depth = water_surface_has_depth(up_surface.x);
    float left_depth = readable_depth(left_surface.x, center_depth);
    float right_depth = readable_depth(right_surface.x, center_depth);
    float down_depth = readable_depth(down_surface.x, center_depth);
    float up_depth = readable_depth(up_surface.x, center_depth);
    float left_thickness = left_has_depth ? max(0.0, left_surface.y) : 0.0;
    float right_thickness = right_has_depth ? max(0.0, right_surface.y) : 0.0;
    float down_thickness = down_has_depth ? max(0.0, down_surface.y) : 0.0;
    float up_thickness = up_has_depth ? max(0.0, up_surface.y) : 0.0;

    vec3 view_dir = normalize(water_surface_camera_position() - position);
    float depth_scale = max(0.0015, center_depth * 0.0025);
    float curvature = abs((left_depth + right_depth + down_depth + up_depth) -
                          (center_depth * 4.0)) /
                      depth_scale;
    float slope = length(vec2(right_depth - left_depth, up_depth - down_depth)) /
                  max(0.0015, depth_scale * 2.0);
    float grazing = pow(clamp(1.0 - dot(normal, view_dir), 0.0, 1.0), 2.0);
    float neighbor_count = float(left_has_depth) + float(right_has_depth) + float(down_has_depth) +
                           float(up_has_depth);
    float open_edge = 1.0 - (neighbor_count * 0.25);
    float thickness_delta =
        length(vec2(right_thickness - left_thickness, up_thickness - down_thickness)) /
        max(0.04, thickness + 0.04);
    float thin_sheet = smoothstep(0.015, 0.12, thickness) *
                       (1.0 - smoothstep(0.45, 1.35, thickness));
    float thickness_gate = smoothstep(0.01, 0.08, thickness) *
                           (1.0 - smoothstep(1.20, 2.40, thickness));

    float edge_signal = open_edge * thin_sheet;
    float crest_signal = (curvature * 0.20) + (slope * 0.10) + (thickness_delta * 0.30) +
                         (grazing * 0.18);
    float sharpness = max(0.2, surface_params.surface_options.y);
    float edge_mask = smoothstep(0.02, 0.18, edge_signal);
    float crest_mask = smoothstep(0.05, 0.26, crest_signal);
    float upper_surface = smoothstep(0.32, 0.74, position.y);
    float upper_patch = upper_surface * thin_sheet * smoothstep(0.36, 0.80, foam_noise(position)) *
                        smoothstep(0.03, 0.16, (thickness_delta * 0.50) + (slope * 0.05) +
                                                    (curvature * 0.03) + (open_edge * 0.20));
    float foam = max(max(edge_mask, crest_mask), upper_patch * 0.85) * thickness_gate;
    foam = pow(clamp(foam, 0.0, 1.0), 1.0 / sharpness);
    return foam * clamp(surface_params.surface_options.x, 0.0, 1.0);
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
    float foam = foam_mask(frag_uv, depth, thickness, position, normal);
    if (view == WATER3D_SURFACE_VIEW_FOAM) {
        float debug_foam =
            clamp(foam / max(0.001, surface_params.surface_options.x), 0.0, 1.0);
        vec3 debug_color = mix(vec3(0.025, 0.035, 0.045), vec3(0.88, 0.93, 0.92), debug_foam);
        out_color = vec4(apply_display_transform(cubey_srgb_to_linear(debug_color)), 1.0);
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
    vec3 foam_color = cubey_srgb_to_linear(vec3(0.86, 0.91, 0.88)) *
                      (0.7 + surface_params.environment_options.z * 0.3);
    color = mix(color, foam_color, foam);

    out_color = vec4(apply_display_transform(color), 1.0);
}
