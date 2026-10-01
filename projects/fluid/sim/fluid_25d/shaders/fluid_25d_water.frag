#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"


layout(push_constant) uniform CatchmentParams {
    mat4 view_projection;
    vec4 grid_cell;
    vec4 camera_wet;
    vec4 presentation;
} params;

layout(location = 0) in vec3 world_position;
layout(location = 1) in vec3 world_normal;
layout(location = 2) in float water_depth;
layout(location = 3) in vec3 water_flow;
layout(location = 4) in float presentation_cue;
layout(location = 5) in float dye_concentration;
layout(location = 0) out vec4 out_color;


void main() {
    if (water_depth <= params.camera_wet.w) {
        discard;
    }

    // The persistent cue field is updated once per fixed simulation step. It
    // is deliberately invisible for calm water, so stillness reads as stable
    // base shading rather than a procedural surface pattern.
    // Keep the early sheet-release settling flow visually calm. The threshold
    // is intentionally above the reporting-only 0.02 m/s slow/pooled
    // threshold: this is a restrained presentation gate, not wetness policy.
    const float calm_speed_m_per_s = 0.025;
    const float full_motion_speed_m_per_s = 0.15;
    float flow_speed = length(water_flow.xy);
    float motion_strength = smoothstep(calm_speed_m_per_s, full_motion_speed_m_per_s,
                                       flow_speed);
    float sparse_highlight = smoothstep(0.58, 0.78, clamp(presentation_cue, 0.0, 1.0)) *
                             motion_strength;
    bool water_isolation = params.presentation.y == 1.0;
    bool flow_inspection = params.presentation.y == 2.0;
    bool transport_inspection = params.presentation.y == 3.0;
    if (water_isolation || flow_inspection || transport_inspection) {
        // Isolation finds the wet footprint; Flow Inspection reserves its
        // motion language for fixed directional quiver arrows. Neither reading
        // mode should compete with the broad advected surface highlight.
        sparse_highlight = 0.0;
    }
    vec3 normal = normalize(world_normal);
    vec3 view_direction = normalize(params.camera_wet.xyz - world_position);
    vec3 light_direction = normalize(vec3(-0.45, 0.82, 0.35));
    float diffuse = max(dot(normal, light_direction), 0.0);
    float fresnel = pow(1.0 - max(dot(normal, view_direction), 0.0), 5.0);
    float depth_factor = clamp(water_depth * 10.0, 0.0, 1.0);
    vec3 shallow = cubey_srgb_to_linear(vec3(0.08, 0.48, 0.72));
    vec3 deep = cubey_srgb_to_linear(vec3(0.015, 0.12, 0.33));
    if (water_isolation) {
        shallow = cubey_srgb_to_linear(vec3(0.08, 0.66, 0.94));
        deep = cubey_srgb_to_linear(vec3(0.01, 0.22, 0.52));
    }
    vec3 color = mix(shallow, deep, depth_factor);
    if (transport_inspection) {
        if (params.presentation.w == 4.0) {
            // Hillside dye is injected at concentration 1. Use the same fixed
            // four-decade logarithmic scale on every frame: 0.001, 0.01, 0.1,
            // and 1 map to palette positions 0.25, 0.5, 0.75, and 1. The
            // maximum tint keeps the blue carrier visible through concentrated
            // parcels; concentrations below 0.0001 remain clear carrier.
            const float dye_scale_minimum = 0.0001;
            const float dye_scale_maximum = 1.0;
            float bounded_concentration = clamp(dye_concentration,
                                                dye_scale_minimum,
                                                dye_scale_maximum);
            float dye_palette_position = clamp(
                log(bounded_concentration / dye_scale_minimum) /
                    log(dye_scale_maximum / dye_scale_minimum),
                0.0, 1.0);
            float dye_visibility = 0.82 * smoothstep(0.0, 1.0, dye_palette_position);
            vec3 dye_magenta = cubey_srgb_to_linear(vec3(1.00, 0.08, 0.62));
            vec3 dye_violet = cubey_srgb_to_linear(vec3(0.52, 0.08, 0.88));
            vec3 dye_color = mix(dye_violet, dye_magenta,
                                 smoothstep(0.15, 0.85, dye_palette_position));
            color = mix(color, dye_color, dye_visibility);
        } else {
            // Preserve the established palette for every other scenario,
            // including its original lower-concentration dye experiments.
            float dye_visibility = smoothstep(0.00025, 0.0030, dye_concentration);
            vec3 dye_magenta = cubey_srgb_to_linear(vec3(1.00, 0.08, 0.62));
            vec3 dye_violet = cubey_srgb_to_linear(vec3(0.52, 0.08, 0.88));
            vec3 dye_color = mix(dye_violet, dye_magenta,
                                 smoothstep(0.0005, 0.0040, dye_concentration));
            color = mix(color, dye_color, dye_visibility);
        }
    }
    color += cubey_srgb_to_linear(vec3(0.34, 0.72, 0.95)) * (0.16 * sparse_highlight);
    color = color * (0.38 + 0.45 * diffuse) + vec3(0.40, 0.68, 0.92) * (0.34 * fresnel);
    float alpha = mix(0.48, 0.76, depth_factor);
    if (params.presentation.z > 0.5 && !water_isolation && !flow_inspection &&
        !transport_inspection) {
        // Opt-in terrain-study presentation only. Rainfall excess wets every
        // cell at sub-centimetre depths; attenuate that thin film in Composite
        // without changing solver wetness, volume, or diagnostic water views.
        alpha *= smoothstep(0.002, 0.050, water_depth);
    }
    if (water_isolation) {
        alpha = mix(0.68, 0.88, depth_factor);
    }
    if (transport_inspection) {
        alpha = max(alpha, 0.72);
    }
    // Premultiplied source-over: the pipeline uses ONE / ONE_MINUS_SRC_ALPHA.
    out_color = vec4(color * alpha, alpha);
}
