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
    vec2 smooth_local = local * local * (3.0 - 2.0 * local);
    float a = hash21(cell);
    float b = hash21(cell + vec2(1.0, 0.0));
    float c = hash21(cell + vec2(0.0, 1.0));
    float d = hash21(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, smooth_local.x), mix(c, d, smooth_local.x), smooth_local.y);
}

vec3 scene_direction() {
    float tan_half_fovy = scene.camera_up_tan_half_fovy.w;
    return normalize(scene.camera_forward.xyz +
                     scene.camera_right_aspect.xyz * frag_ndc.x *
                         scene.camera_right_aspect.w * tan_half_fovy -
                     scene.camera_up_tan_half_fovy.xyz * frag_ndc.y * tan_half_fovy);
}

vec3 seafloor_color(vec3 world_position, float ray_distance) {
    float coarse = value_noise(world_position.xz * 0.018);
    float fine = value_noise(world_position.xz * 0.071 + vec2(11.3, -7.1));
    vec3 sand = cubey_srgb_to_linear(vec3(0.50, 0.46, 0.34));
    vec3 rock = cubey_srgb_to_linear(vec3(0.23, 0.29, 0.28));
    vec3 grass = cubey_srgb_to_linear(vec3(0.13, 0.24, 0.20));
    vec3 base = mix(sand, rock, smoothstep(0.38, 0.86, coarse));
    base = mix(base, grass, smoothstep(0.62, 0.92, fine) * 0.32);
    float brightness = max(scene.scene_options.y, 0.0);
    float fog = smoothstep(900.0, 4200.0, ray_distance) * clamp(scene.scene_options.z, 0.0, 1.0);
    vec3 horizon_dir = normalize(vec3(-scene.camera_forward.x, 0.10, -scene.camera_forward.z));
    return mix(base * brightness, ocean_sky_color(horizon_dir), fog);
}

void main() {
    vec3 direction = scene_direction();
    vec3 sky = ocean_sky_color(direction);
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
