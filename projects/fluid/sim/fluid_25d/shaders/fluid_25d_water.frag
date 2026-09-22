#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"

layout(set = 0, binding = 5, std430) readonly buffer EndpointMarkers {
    vec4 source_xy_outlet_xy;
} endpoint_markers;

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

float endpoint_annulus(vec2 grid_position, vec2 endpoint, float radius_cells) {
    if (any(lessThan(endpoint, vec2(0.0)))) {
        return 0.0;
    }
    float radial_distance = length(grid_position - endpoint);
    return 1.0 - smoothstep(0.60, 1.45, abs(radial_distance - radius_cells));
}

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
        // The injected material concentration is deliberately low (one
        // percent), so its palette range is expressed in concentration rather
        // than raw q. The blue carrier water remains visible at the leading
        // and trailing edge; sufficiently dyed parcels become unambiguous
        // magenta/violet without borrowing Flow Inspection's arrow language.
        float dye_visibility = smoothstep(0.00025, 0.0030, dye_concentration);
        vec3 dye_magenta = cubey_srgb_to_linear(vec3(1.00, 0.08, 0.62));
        vec3 dye_violet = cubey_srgb_to_linear(vec3(0.52, 0.08, 0.88));
        vec3 dye_color = mix(dye_violet, dye_magenta,
                             smoothstep(0.0005, 0.0040, dye_concentration));
        color = mix(color, dye_color, dye_visibility);
    }
    color += cubey_srgb_to_linear(vec3(0.34, 0.72, 0.95)) * (0.16 * sparse_highlight);
    color = color * (0.38 + 0.45 * diffuse) + vec3(0.40, 0.68, 0.92) * (0.34 * fresnel);
    float alpha = mix(0.48, 0.76, depth_factor);
    if (water_isolation) {
        alpha = mix(0.68, 0.88, depth_factor);
    }
    if (transport_inspection) {
        alpha = max(alpha, 0.72);
    }
    // Keep endpoint rings legible where the shallow source/outlet ribbon is
    // translucent. The terrain pass draws the same rings over dry bed, so a
    // route remains labelled even after a local sink removes its water.
    vec2 grid_position = world_position.xz / params.grid_cell.z +
                         0.5 * vec2(params.grid_cell.x - 1.0, params.grid_cell.y - 1.0);
    float radius_cells = clamp(0.045 * min(params.grid_cell.x, params.grid_cell.y), 3.0, 6.0);
    float source_marker = endpoint_annulus(grid_position,
                                            endpoint_markers.source_xy_outlet_xy.xy,
                                            radius_cells);
    float outlet_marker = endpoint_annulus(grid_position,
                                            endpoint_markers.source_xy_outlet_xy.zw,
                                            radius_cells);
    vec3 source_color = cubey_srgb_to_linear(vec3(0.16, 0.88, 0.34));
    vec3 outlet_color = cubey_srgb_to_linear(vec3(1.00, 0.56, 0.08));
    color = mix(color, source_color, 0.94 * source_marker);
    color = mix(color, outlet_color, 0.94 * outlet_marker);
    alpha = max(alpha, 0.82 * max(source_marker, outlet_marker));
    // Premultiplied source-over: the pipeline uses ONE / ONE_MINUS_SRC_ALPHA.
    out_color = vec4(color * alpha, alpha);
}
