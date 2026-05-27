#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"
#include "cubey/pbr.glsl"

layout(push_constant) uniform OceanParams {
    mat4 view_projection;
    vec4 camera_time;
    vec4 mesh_options;
    vec4 wave_options;
    vec4 detail_options;
    vec4 shading_options;
    vec4 display_transform;
    vec4 disturbance;
    vec4 debug_options;
} ocean;

layout(location = 0) in vec3 frag_world_position;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec4 frag_wave;

layout(location = 0) out vec4 out_color;

const uint OCEAN_VIEW_FINAL = 0u;
const uint OCEAN_VIEW_HEIGHT = 1u;
const uint OCEAN_VIEW_NORMAL = 2u;
const uint OCEAN_VIEW_FOAM = 3u;
const uint OCEAN_VIEW_REFLECTION = 4u;
const uint OCEAN_VIEW_REFRACTION = 5u;
const uint OCEAN_VIEW_DEPTH = 6u;

vec3 sky_color(vec3 direction) {
    direction = normalize(direction);
    float horizon = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 horizon_color = cubey_srgb_to_linear(vec3(0.66, 0.78, 0.89));
    vec3 zenith_color = cubey_srgb_to_linear(vec3(0.045, 0.16, 0.34));
    vec3 color = mix(horizon_color, zenith_color, pow(horizon, 1.65));

    vec3 sun_direction = normalize(vec3(-0.34, 0.38, 0.86));
    float sun_disk = pow(max(dot(direction, sun_direction), 0.0), 780.0);
    float sun_glow = pow(max(dot(direction, sun_direction), 0.0), 18.0);
    color += cubey_srgb_to_linear(vec3(1.0, 0.78, 0.43)) * sun_disk * 18.0;
    color += cubey_srgb_to_linear(vec3(0.95, 0.54, 0.22)) * sun_glow * 0.55;
    return color;
}

float hash21(vec2 value) {
    vec3 p = fract(vec3(value.xyx) * vec3(443.897, 441.423, 437.195));
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

vec3 add_micro_normal(vec3 base_normal, vec2 position, float time) {
    float wind = ocean.wave_options.y;
    vec2 wind_dir = normalize(vec2(cos(wind), sin(wind)));
    vec2 cross_dir = vec2(-wind_dir.y, wind_dir.x);
    float n0 = sin(dot(position, wind_dir) * 0.33 - time * 2.1);
    float n1 = sin(dot(position, cross_dir) * 0.61 + time * 1.7);
    float n2 = sin(dot(position, normalize(wind_dir + cross_dir * 0.45)) * 1.37 - time * 3.3);
    vec2 gradient = wind_dir * (n0 * 0.040) + cross_dir * (n1 * 0.026) +
                    normalize(wind_dir + cross_dir * 0.45) * (n2 * 0.012);
    float detail_fade = 1.0 - smoothstep(180.0, 980.0, frag_wave.z);
    vec3 normal = base_normal + vec3(-gradient.x, 0.0, -gradient.y) *
                                    ocean.detail_options.y * detail_fade;
    return normalize(normal);
}

vec3 refraction_color(float depth, float foam) {
    vec3 shallow_water = cubey_srgb_to_linear(vec3(0.18, 0.70, 0.78));
    vec3 deep_water = cubey_srgb_to_linear(vec3(0.010, 0.075, 0.145));
    vec3 sand = cubey_srgb_to_linear(vec3(0.58, 0.55, 0.43));
    float transmittance = exp(-max(ocean.shading_options.x, 0.0) * depth);
    vec3 water_tint = mix(deep_water, shallow_water, clamp(transmittance * 1.25, 0.0, 1.0));
    vec3 bottom = mix(water_tint, sand, clamp(transmittance * 0.55, 0.0, 1.0));
    return mix(bottom, cubey_srgb_to_linear(vec3(0.80, 0.94, 0.93)), foam * 0.14);
}

float foam_mask(vec3 normal, float depth) {
    float crest = frag_wave.y;
    float slope = 1.0 - clamp(normal.y, 0.0, 1.0);
    float slope_foam = smoothstep(ocean.detail_options.w, ocean.detail_options.w + 0.35,
                                  slope * ocean.wave_options.w * 4.5);
    float shallow_foam = (1.0 - smoothstep(1.5, 11.0, depth)) * ocean.shading_options.z;
    float noisy_breakup = 0.62 + 0.38 * hash21(floor(frag_world_position.xz * 0.075));
    return clamp((crest + slope_foam * 0.32 + shallow_foam * 0.48) *
                     ocean.detail_options.z * noisy_breakup,
                 0.0, 1.0);
}

vec3 debug_height_color(float height) {
    float value = clamp(height * 0.06 + 0.5, 0.0, 1.0);
    vec3 low = cubey_srgb_to_linear(vec3(0.04, 0.18, 0.42));
    vec3 mid = cubey_srgb_to_linear(vec3(0.12, 0.65, 0.78));
    vec3 high = cubey_srgb_to_linear(vec3(0.96, 0.94, 0.78));
    return value < 0.5 ? mix(low, mid, value * 2.0) : mix(mid, high, (value - 0.5) * 2.0);
}

vec3 apply_display(vec3 color) {
    return cubey_pbr_apply_display_transform(color, ocean.display_transform);
}

void main() {
    uint view = uint(ocean.debug_options.x + 0.5);
    vec3 camera_position = ocean.camera_time.xyz;
    vec3 view_dir = normalize(camera_position - frag_world_position);
    vec3 normal = add_micro_normal(normalize(frag_normal), frag_world_position.xz,
                                   ocean.camera_time.w);
    float depth = max(frag_wave.w, 0.0);
    float foam = foam_mask(normal, depth);

    vec3 reflection_dir = reflect(-view_dir, normal);
    vec3 reflection = sky_color(reflection_dir);
    vec3 refraction = refraction_color(depth, foam);
    float fresnel = 0.020 + 0.980 * pow(clamp(1.0 - dot(normal, view_dir), 0.0, 1.0), 5.0);
    vec3 water = mix(refraction, reflection, fresnel);
    vec3 foam_color = cubey_srgb_to_linear(vec3(0.82, 0.94, 0.91));
    water = mix(water, foam_color, foam);

    float horizon_fog = smoothstep(ocean.mesh_options.y * 0.28, ocean.mesh_options.y * 0.92,
                                   frag_wave.z) *
                        ocean.mesh_options.w;
    vec3 horizon_dir = normalize(vec3(-view_dir.x, 0.055, -view_dir.z));
    vec3 color = mix(water, sky_color(horizon_dir), horizon_fog);

    if (view == OCEAN_VIEW_HEIGHT) {
        color = debug_height_color(frag_wave.x);
    } else if (view == OCEAN_VIEW_NORMAL) {
        color = normal * 0.5 + 0.5;
    } else if (view == OCEAN_VIEW_FOAM) {
        color = cubey_srgb_to_linear(vec3(foam));
    } else if (view == OCEAN_VIEW_REFLECTION) {
        color = reflection;
    } else if (view == OCEAN_VIEW_REFRACTION) {
        color = refraction;
    } else if (view == OCEAN_VIEW_DEPTH) {
        float depth_value = 1.0 - exp(-depth * 0.055);
        color = cubey_srgb_to_linear(mix(vec3(0.85, 0.74, 0.47), vec3(0.02, 0.15, 0.35),
                                         depth_value));
    }

    out_color = vec4(apply_display(color), 1.0);
}
