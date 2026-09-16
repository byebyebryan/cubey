#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"

layout(push_constant) uniform CatchmentParams {
    mat4 view_projection;
    vec4 grid_cell;
    vec4 camera_wet;
    vec4 animation;
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

    // Keep the cue tied to the physical velocity field. The spatial scale is
    // deliberately several cells wide for the 30 m terrain captures, while
    // the lower bound avoids sub-cell aliasing on small fixture grids.
    const float calm_speed_m_per_s = 0.015;
    const float full_motion_speed_m_per_s = 0.12;
    const float direction_epsilon_m_per_s = 0.00001;
    float flow_speed = length(water_flow.xy);
    float motion_strength = smoothstep(calm_speed_m_per_s, full_motion_speed_m_per_s,
                                       flow_speed);
    vec2 flow_direction = flow_speed > direction_epsilon_m_per_s
                              ? water_flow.xy / flow_speed
                              : vec2(1.0, 0.0);
    vec2 cross_direction = vec2(-flow_direction.y, flow_direction.x);
    float primary_frequency = 0.54 / max(params.grid_cell.z, 10.0);
    float secondary_frequency = primary_frequency * 0.68;
    vec2 wave_vector_a = flow_direction * primary_frequency +
                         cross_direction * (primary_frequency * 0.34);
    vec2 wave_vector_b = flow_direction * secondary_frequency -
                         cross_direction * (secondary_frequency * 0.58);
    vec2 advected_position = world_position.xz - water_flow.xy * params.animation.x;
    float phase_a = dot(advected_position, wave_vector_a) + 0.45;
    float phase_b = dot(advected_position, wave_vector_b) + 2.15;
    float wave_a = sin(phase_a);
    float wave_b = sin(phase_b);
    float advected_cue = 0.5 + 0.5 * (0.62 * wave_a + 0.38 * wave_b);
    float settled_cue =
        0.5 + 0.5 * sin(dot(world_position.xz, vec2(primary_frequency, -secondary_frequency)) +
                          0.85);
    float flow_cue = mix(settled_cue, advected_cue, motion_strength);
    vec2 detail_gradient = wave_vector_a * (0.62 * cos(phase_a)) +
                           wave_vector_b * (0.38 * cos(phase_b));
    vec3 normal = normalize(world_normal +
                            vec3(detail_gradient.x, 0.0, detail_gradient.y) *
                                (2.4 * motion_strength));
    vec3 view_direction = normalize(params.camera_wet.xyz - world_position);
    vec3 light_direction = normalize(vec3(-0.45, 0.82, 0.35));
    float diffuse = max(dot(normal, light_direction), 0.0);
    float fresnel = pow(1.0 - max(dot(normal, view_direction), 0.0), 5.0);
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
