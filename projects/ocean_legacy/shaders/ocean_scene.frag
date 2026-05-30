#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"
#include "ocean_atmosphere.glsl"

layout(push_constant) uniform SceneParams {
    mat4 view_projection;
    vec4 camera_time;
    vec4 camera_right_aspect;
    vec4 camera_up_tan_half_fovy;
    vec4 camera_forward;
    vec4 scene_options;
} scene;

layout(location = 0) in vec2 frag_ndc;
layout(location = 0) out vec4 out_color;

float hash21(vec2 value) {
    vec3 p = fract(vec3(value.xyx) * vec3(443.897, 441.423, 437.195));
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

float value_noise(vec2 value) {
    vec2 cell = floor(value);
    vec2 local = fract(value);
    vec2 smooth_local = local * local * local * (local * (local * 6.0 - 15.0) + 10.0);
    float a = hash21(cell);
    float b = hash21(cell + vec2(1.0, 0.0));
    float c = hash21(cell + vec2(0.0, 1.0));
    float d = hash21(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, smooth_local.x), mix(c, d, smooth_local.x), smooth_local.y);
}

mat2 rotation2(float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return mat2(c, -s, s, c);
}

float fbm_noise(vec2 position) {
    float value = 0.0;
    float amplitude = 0.52;
    vec2 p = position;
    for (int octave = 0; octave < 5; ++octave) {
        value += value_noise(p) * amplitude;
        p = rotation2(0.73) * p * 2.03 + vec2(17.1, -9.4);
        amplitude *= 0.50;
    }
    return value;
}

vec2 floor_domain_warp(vec2 position) {
    vec2 broad =
        vec2(fbm_noise(rotation2(0.42) * position * 0.0028 + vec2(6.0, -12.0)),
             fbm_noise(rotation2(-0.67) * position * 0.0028 + vec2(-18.0, 4.0))) -
        vec2(0.5);
    vec2 medium =
        vec2(fbm_noise(rotation2(-0.19) * position * 0.010 + vec2(22.0, 11.0)),
             fbm_noise(rotation2(0.88) * position * 0.010 + vec2(-9.0, -27.0))) -
        vec2(0.5);
    return broad * 135.0 + medium * 26.0;
}

float seafloor_height(vec2 floor_position) {
    vec2 warped_position = floor_position + floor_domain_warp(floor_position);
    float broad = fbm_noise(rotation2(0.31) * warped_position * 0.0036 + vec2(4.7, -2.1));
    float medium = fbm_noise(rotation2(-0.58) * warped_position * 0.011 + vec2(-11.3, 8.6));
    float fine = fbm_noise(rotation2(0.96) * warped_position * 0.038 + vec2(31.0, 19.0));
    return clamp(broad * 0.52 + medium * 0.32 + fine * 0.16, 0.0, 1.0);
}

vec3 scene_direction() {
    float tan_half_fovy = scene.camera_up_tan_half_fovy.w;
    return normalize(scene.camera_forward.xyz +
                     scene.camera_right_aspect.xyz * frag_ndc.x *
                         scene.camera_right_aspect.w * tan_half_fovy -
                     scene.camera_up_tan_half_fovy.xyz * frag_ndc.y * tan_half_fovy);
}

vec3 seafloor_color(vec3 world_position, float ray_distance) {
    vec2 floor_position = world_position.xz;
    float footprint = max(length(dFdx(floor_position)), length(dFdy(floor_position)));
    float detail_strength = max(scene.scene_options.w, 0.0);
    float detail_weight = (1.0 - smoothstep(8.0, 72.0, footprint)) * detail_strength;
    float close_detail_weight = (1.0 - smoothstep(3.0, 34.0, footprint)) * detail_strength;
    vec2 warped_position = floor_position + floor_domain_warp(floor_position);

    float broad = fbm_noise(rotation2(0.31) * warped_position * 0.0036 + vec2(4.7, -2.1));
    float medium = fbm_noise(rotation2(-0.58) * warped_position * 0.011 + vec2(-11.3, 8.6));
    float fine = fbm_noise(rotation2(0.96) * warped_position * 0.038 + vec2(31.0, 19.0));
    float height = seafloor_height(floor_position);
    float sample_step = max(footprint * 0.75, 5.0);
    float height_x = seafloor_height(floor_position + vec2(sample_step, 0.0));
    float height_z = seafloor_height(floor_position + vec2(0.0, sample_step));
    vec3 floor_normal = normalize(vec3((height - height_x) * 3.5 * detail_weight, 1.0,
                                       (height - height_z) * 3.5 * detail_weight));

    vec3 sand = cubey_srgb_to_linear(vec3(0.62, 0.56, 0.40));
    vec3 wet_sand = cubey_srgb_to_linear(vec3(0.40, 0.43, 0.36));
    vec3 silt = cubey_srgb_to_linear(vec3(0.27, 0.35, 0.32));
    vec3 shell = cubey_srgb_to_linear(vec3(0.76, 0.68, 0.48));
    float sediment_mix =
        clamp(0.34 + (height - 0.5) * 1.24 * detail_strength +
                  (medium - 0.5) * 0.18 * detail_strength,
              0.0, 1.0);
    vec3 base = mix(sand, wet_sand, sediment_mix);
    base = mix(base, silt,
               clamp(((height - 0.43) * 0.34 + (medium - 0.5) * 0.10) * detail_strength,
                     0.0, 0.24));
    base = mix(base, shell, smoothstep(0.68, 0.98, fine + broad * 0.08) * 0.10 *
                             close_detail_weight);

    float sun = max(dot(floor_normal, ocean_sun_direction()), 0.0);
    float relief = 0.88 + sun * 0.22;
    float ambient = 0.86 + (height - 0.5) * 0.64 * detail_strength +
                    (fine - 0.5) * 0.08 * close_detail_weight;
    float ripple_phase =
        dot(warped_position, vec2(0.034, 0.013)) + broad * 5.2 + medium * 2.1;
    float sand_ripple = pow(0.5 + 0.5 * cos(ripple_phase), 4.0);
    ambient += sand_ripple * 0.10 * close_detail_weight;
    base *= relief * ambient;

    float brightness = max(scene.scene_options.y, 0.0);
    float fog = smoothstep(900.0, 4200.0, ray_distance) * clamp(scene.scene_options.z, 0.0, 1.0);
    vec3 horizon_dir = normalize(vec3(-scene.camera_forward.x, 0.10, -scene.camera_forward.z));
    return mix(base * brightness, ocean_sky_color(horizon_dir), fog);
}

void main() {
    vec3 direction = scene_direction();
    vec3 sky = ocean_sky_color(direction);
    if (scene.scene_options.w < 0.0) {
        out_color = vec4(sky, 1.0);
        gl_FragDepth = 1.0;
        return;
    }

    float seafloor_y = -max(scene.scene_options.x, 0.1);
    float t = (seafloor_y - scene.camera_time.y) / direction.y;
    if (direction.y >= -0.0001 || t <= 0.0) {
        out_color = vec4(sky, 1.0);
        gl_FragDepth = 1.0;
        return;
    }

    vec3 world_position = scene.camera_time.xyz + direction * t;
    vec4 clip = scene.view_projection * vec4(world_position, 1.0);
    float depth = clip.z / max(clip.w, 0.0001);
    if (depth < 0.0 || depth > 1.0) {
        out_color = vec4(sky, 1.0);
        gl_FragDepth = 1.0;
        return;
    }

    vec3 bottom = seafloor_color(world_position, t);
    float sky_fog = smoothstep(2200.0, 7200.0, t);
    out_color = vec4(mix(bottom, sky, sky_fog), 1.0);
    gl_FragDepth = clamp(depth, 0.0, 1.0);
}
