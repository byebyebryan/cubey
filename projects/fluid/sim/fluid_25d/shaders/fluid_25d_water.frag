#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"
#include "fluid_25d_surface_sampling.glsl"

layout(set = 0, binding = 1, std430) readonly buffer DepthField { float values[]; } depth;
layout(set = 0, binding = 8, std430) readonly buffer DisplayCoverage {
    vec4 grid;
    float values[];
} display_coverage;

layout(push_constant) uniform CatchmentParams {
    mat4 view_projection;
    vec4 grid_cell;
    vec4 camera_wet;
    vec4 presentation;
    vec4 terrain_palette;
} params;

layout(location = 0) in vec3 world_position;
layout(location = 1) in vec3 world_normal;
layout(location = 2) in float triangle_water_depth;
layout(location = 3) in vec3 water_flow;
layout(location = 4) in float presentation_cue;
layout(location = 5) in float dye_concentration;
layout(location = 6) in vec2 field_coordinate;
layout(location = 0) out vec4 out_color;

float reconstructed_bank_coverage() {
    vec2 f;
    uvec4 i = fluid25d_quad_indices(field_coordinate * display_coverage.grid.z,
                                   uvec2(display_coverage.grid.xy), f);
    return clamp(fluid25d_bilinear_sample(vec4(display_coverage.values[i.x],
        display_coverage.values[i.y], display_coverage.values[i.z], display_coverage.values[i.w]), f), 0.0, 1.0);
}

void main() {
    uint options = uint(params.terrain_palette.w);
    uint diagnostic = options & 7u;
    float water_depth = triangle_water_depth;
    if ((options & 8u) != 0u) {
        vec2 f;
        uvec4 i = fluid25d_quad_indices(field_coordinate, uvec2(params.grid_cell.xy), f);
        water_depth = fluid25d_supported_depth(vec4(depth.values[i.x], depth.values[i.y],
                                                    depth.values[i.z], depth.values[i.w]),
                                               f, params.camera_wet.w);
    }
    if ((options & 16u) != 0u) {
        // Geometry/material share the subdivided vertex depth. Only the
        // native wet-support fence is independent; never fill a dry gap by
        // spreading a positive cubic field into it.
        vec2 f;
        uvec4 i = fluid25d_quad_indices(field_coordinate, uvec2(params.grid_cell.xy), f);
        vec4 wet = vec4(greaterThan(vec4(depth.values[i.x],depth.values[i.y],
                                          depth.values[i.z],depth.values[i.w]),
                                    vec4(params.camera_wet.w)));
        if (fluid25d_bilinear_sample(wet,f) <= 0.5) water_depth = 0.0;
    }
    // Evaluate coverage derivatives before any data-dependent discard.
    float depth_pixel_width = max(fwidth(water_depth), 0.000001);
    if (diagnostic == 5u) {
        // Cyan: both readings wet; magenta: interpolated-only; yellow: native
        // cell-only. This explicitly exposes display footprint vs saved mask.
        ivec2 cell = clamp(ivec2(floor(field_coordinate + 0.5)), ivec2(0),
                           ivec2(params.grid_cell.xy) - 1);
        bool native_wet = depth.values[cell.y * int(params.grid_cell.x) + cell.x] > params.camera_wet.w;
        bool display_wet = water_depth > params.camera_wet.w;
        if (!native_wet && !display_wet) discard;
        out_color = vec4(native_wet && display_wet ? vec3(0.0, 0.8, 0.8)
                        : native_wet ? vec3(1.0, 0.8, 0.0) : vec3(0.9, 0.0, 0.7), 1.0);
        return;
    }
    if (water_depth <= params.camera_wet.w) {
        discard;
    }
    if (diagnostic == 7u) {
        // Fixed physical depth bands, independent of film opacity/lighting.
        vec3 c = water_depth < 0.002 ? vec3(0.25)
               : water_depth < 0.010 ? vec3(1.0,0.8,0.0)
               : water_depth < 0.026 ? vec3(0.9,0.0,0.7)
               : water_depth < 0.050 ? vec3(0.0,0.8,0.8) : vec3(0.1,0.2,1.0);
        out_color = vec4(c,1.0);
        return;
    }
    if (diagnostic == 1u || diagnostic == 3u || diagnostic == 4u || diagnostic == 6u) {
        vec3 debug_color = vec3(0.0, 0.65, 0.9);
        if (diagnostic == 3u) debug_color = normalize(world_normal) * 0.5 + 0.5;
        if (diagnostic == 4u) {
            float subdivision = (options & 16u) != 0u ? float((options>>5u)&7u) : 1.0;
            vec2 f = fract(field_coordinate*subdivision);
            vec3 edge = vec3(min(f.x, 1.0-f.x), min(f.y, 1.0-f.y), abs(f.x-f.y));
            vec3 width = max(fwidth(edge), vec3(0.00001));
            float interior = min(min(smoothstep(vec3(0), width, edge).x,
                                     smoothstep(vec3(0), width, edge).y),
                                     smoothstep(vec3(0), width, edge).z);
            debug_color *= mix(0.1, 1.0, interior);
        }
        // Keep the same Composite visibility policy: otherwise an opaque
        // sub-centimetre rain film hides the banks we are diagnosing.
        float coverage = 1.0;
        if (params.presentation.y == 0.0 && mod(params.presentation.z, 2.0) > 0.5)
            coverage *= smoothstep(0.002, 0.050, water_depth);
        coverage *= smoothstep(0.0, depth_pixel_width,
                               water_depth - params.camera_wet.w);
        out_color = vec4(debug_color * coverage, coverage);
        return;
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
    bool native_motion = params.presentation.w >= 5.0;
    bool native_readable = params.presentation.w >= 6.0;
    bool hillside_depth_cues = params.presentation.w >= 4.0 && params.presentation.z >= 2.0;
    if (native_motion)
        sparse_highlight = smoothstep(0.48, 0.70, clamp(presentation_cue, 0.0, 1.0)) *
                           motion_strength;
    if (water_isolation || flow_inspection || transport_inspection) {
        // Isolation finds the wet footprint; Flow Inspection reserves its
        // motion language for fixed directional quiver arrows. Neither reading
        // mode should compete with the broad advected surface highlight.
        sparse_highlight = 0.0;
    }
    // Bit 8 is water-only. It suppresses this procedural contribution without
    // changing depth, coverage, normals, velocity, or any reconstruction path.
    if ((options & 256u) != 0u) sparse_highlight = 0.0;
    vec3 normal = normalize(world_normal);
    vec3 view_direction = normalize(params.camera_wet.xyz - world_position);
    vec3 light_direction = normalize(vec3(-0.45, 0.82, 0.35));
    float diffuse = max(dot(normal, light_direction), 0.0);
    float fresnel = pow(1.0 - max(dot(normal, view_direction), 0.0), 5.0);
    float depth_factor = clamp(water_depth * 10.0, 0.0, 1.0);
    if (hillside_depth_cues) {
        // Fixed physical depths: 1 cm / 10 cm / 1 m / 10 m. Keep thin
        // runoff distinct from deep collection instead of saturating at 10 cm.
        depth_factor = clamp(log(max(water_depth, 0.01) / 0.01) / log(1000.0), 0.0, 1.0);
    }
    vec3 shallow = cubey_srgb_to_linear(vec3(0.08, 0.48, 0.72));
    vec3 deep = cubey_srgb_to_linear(vec3(0.015, 0.12, 0.33));
    if (native_readable) {
        shallow = cubey_srgb_to_linear(vec3(0.10, 0.57, 0.78));
        deep = cubey_srgb_to_linear(vec3(0.025, 0.17, 0.36));
    }
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
    color += cubey_srgb_to_linear(vec3(0.34, 0.72, 0.95)) *
             ((native_motion ? 0.32 : 0.16) * sparse_highlight);
    if (diagnostic != 2u)
        color = color * (0.38 + 0.45 * diffuse) + vec3(0.40, 0.68, 0.92) * (0.34 * fresnel);
    float alpha = mix(0.48, 0.76, depth_factor);
    if (native_readable) {
        // Stable view/light response, not a wall-clock wave animation. The
        // depth palette is unchanged by this style flag; reconstruction is a
        // separate native-only opt-in.
        vec3 half_direction = normalize(light_direction + view_direction);
        float glint = pow(max(dot(normal, half_direction), 0.0), 48.0);
        if (diagnostic != 2u) color += vec3(0.07, 0.09, 0.11) * glint;
        alpha = mix(0.56, 0.84, depth_factor);
    }
    if (mod(params.presentation.z, 2.0) > 0.5 && !water_isolation && !flow_inspection &&
        !transport_inspection) {
        // Opt-in terrain-study presentation only. Rainfall excess wets every
        // cell at sub-centimetre depths; attenuate that thin film in Composite
        // without changing solver wetness, volume, or diagnostic water views.
        // Sidecar changes only Composite display coverage. Physical depth,
        // surface geometry, colors, wet/dry and other diagnostics remain raw.
        alpha *= ((options & 512u) != 0u && diagnostic == 0u)
                     ? reconstructed_bank_coverage()
                     : smoothstep(0.002, 0.050, water_depth);
    }
    if (water_isolation) {
        alpha = mix(0.68, 0.88, depth_factor);
    }
    if (transport_inspection) {
        alpha = max(alpha, 0.72);
    }
    if (hillside_depth_cues) {
        // Only attenuate the already-wet fragment near the numerical wet
        // threshold. This is screen-space coverage AA, not extra wet cells or
        // geometry reconstruction (the B-spline experiment is a separate path).
        float shoreline_width = depth_pixel_width;
        if (native_readable) shoreline_width *= 1.25;
        alpha *= smoothstep(0.0, shoreline_width, water_depth - params.camera_wet.w);
    }
    // Premultiplied source-over: the pipeline uses ONE / ONE_MINUS_SRC_ALPHA.
    out_color = vec4(color * alpha, alpha);
}
