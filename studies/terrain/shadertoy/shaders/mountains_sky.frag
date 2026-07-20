#version 450
#extension GL_GOOGLE_include_directive : require

#include "reference_frame.glsl"
#include "mountains.glsl"

layout(location = 0) out vec4 out_color;

void main() {
    cameraPos = reference_frame.camera_position_time.xyz;
    const vec2 shader_toy_frag_coord =
        vec2(gl_FragCoord.x, iResolution.y - gl_FragCoord.y);
    const vec2 xy = -1.0 + 2.0 * shader_toy_frag_coord / iResolution.xy;
    const vec2 uv = xy * vec2(iResolution.x / iResolution.y, 1.0);
    const vec3 ray_direction = normalize(
        uv.x * reference_frame.camera_right.xyz +
        uv.y * reference_frame.camera_up.xyz +
        1.5 * reference_frame.camera_forward.xyz);
    const vec3 color = GetClouds(GetSky(ray_direction), ray_direction);
    out_color = vec4(PostEffects(color, uv), 1.0);
}
