#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"

layout(push_constant) uniform CatchmentParams {
    mat4 view_projection;
    vec4 grid_cell;
    vec4 camera_wet;
} params;

layout(location = 0) in vec3 world_position;
layout(location = 1) in vec3 world_normal;
layout(location = 2) in float water_depth;
layout(location = 3) in vec3 water_flow;
layout(location = 0) out vec4 out_color;

void main() {
    if (water_depth <= params.camera_wet.w) {
        discard;
    }
    vec3 normal = normalize(world_normal);
    vec3 view_direction = normalize(params.camera_wet.xyz - world_position);
    vec3 light_direction = normalize(vec3(-0.45, 0.82, 0.35));
    float diffuse = max(dot(normal, light_direction), 0.0);
    float fresnel = pow(1.0 - max(dot(normal, view_direction), 0.0), 5.0);
    float flow_speed = length(water_flow.xy);
    float flow_cue = 0.5 + 0.5 * sin(flow_speed * 5.0 + world_position.x * 0.35 +
                                      world_position.z * 0.25);
    float depth_factor = clamp(water_depth * 10.0, 0.0, 1.0);
    vec3 shallow = cubey_srgb_to_linear(vec3(0.08, 0.48, 0.72));
    vec3 deep = cubey_srgb_to_linear(vec3(0.015, 0.12, 0.33));
    vec3 color = mix(shallow, deep, depth_factor);
    color += cubey_srgb_to_linear(vec3(0.34, 0.72, 0.95)) * (0.18 * flow_cue);
    color = color * (0.38 + 0.45 * diffuse) + vec3(0.40, 0.68, 0.92) * (0.34 * fresnel);
    float alpha = mix(0.48, 0.76, depth_factor);
    // Premultiplied source-over: the pipeline uses ONE / ONE_MINUS_SRC_ALPHA.
    out_color = vec4(color * alpha, alpha);
}
