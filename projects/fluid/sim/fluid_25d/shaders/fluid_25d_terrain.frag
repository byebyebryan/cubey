#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"


layout(location = 0) in vec3 world_position;
layout(location = 1) in vec3 world_normal;
layout(location = 2) in vec2 world_xz;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform CatchmentParams {
    mat4 view_projection;
    vec4 grid_cell;
    vec4 camera_wet;
    vec4 presentation;
    vec4 terrain_palette;
} params;


void main() {
    vec3 normal = normalize(world_normal);
    vec3 light_direction = normalize(vec3(-0.45, 0.82, 0.35));
    float diffuse = max(dot(normal, light_direction), 0.0);
    float contour = 0.006 * sin(world_position.y * 0.75 + world_xz.x * 0.03);
    // Kilometre-scale uplands have hundreds of these cosmetic stripes.
    // Omit them only for the macro study so actual facets/slopes carry relief.
    if (params.presentation.w > 3.5) {
        contour = 0.0;
    }
    vec3 lowland = cubey_srgb_to_linear(vec3(0.105, 0.205, 0.105));
    vec3 highland = cubey_srgb_to_linear(vec3(0.46, 0.31, 0.16));
    float elevation = clamp((world_position.y + 2.0) * 0.035, 0.0, 1.0);
    vec3 albedo = mix(lowland, highland, elevation) + vec3(contour);
    if (params.presentation.w > 2.5 && params.presentation.w < 3.5) {
        // The dry-start headwaters control has sub-metre numerical banks over
        // a 256 m footprint. Its vertex-space relief is selected by the
        // scenario render scale; use physical elevation plus those rendered
        // slopes to separate the Y-shaped bed and shoulders without modifying
        // the terrain buffer or solver state.
        float terrain_height_m = world_position.y / max(params.grid_cell.w, 1.0e-6);
        float elevation_cue = smoothstep(0.05, 1.45, terrain_height_m);
        float slope = 1.0 - normal.y;
        float bank_cue = smoothstep(0.003, 0.035, slope);
        vec3 headwater_bed = cubey_srgb_to_linear(vec3(0.075, 0.19, 0.105));
        vec3 headwater_upland = cubey_srgb_to_linear(vec3(0.39, 0.34, 0.20));
        vec3 headwater_bank = cubey_srgb_to_linear(vec3(0.48, 0.37, 0.20));
        albedo = mix(headwater_bed, headwater_upland, 0.58 * elevation_cue);
        albedo = mix(albedo, headwater_bank, 0.82 * bank_cue);
    } else if (params.presentation.w > 1.5) {
        // Mountain source/outlet uses a pinned immutable crop. Normalize only
        // its render-space elevation against that crop's fixed physical range
        // so broad low valley, high ridge, and steep shoulders survive the
        // kilometre-scale overview. The GPU solver still reads the untouched
        // terrain buffer in metres; this branch is a fragment-only palette.
        float terrain_height_m = world_position.y / max(params.grid_cell.w, 1.0e-6);
        float mountain_elevation = smoothstep(-84.0, 644.0, terrain_height_m);
        float slope = 1.0 - normal.y;
        float slope_cue = smoothstep(0.004, 0.120, slope);
        vec3 mountain_valley = cubey_srgb_to_linear(vec3(0.055, 0.175, 0.105));
        vec3 mountain_ridge = cubey_srgb_to_linear(vec3(0.61, 0.43, 0.22));
        vec3 mountain_stone = cubey_srgb_to_linear(vec3(0.30, 0.245, 0.175));
        albedo = mix(mountain_valley, mountain_ridge, pow(mountain_elevation, 0.82));
        albedo = mix(albedo, mountain_stone, 0.62 * slope_cue);
    } else if (params.presentation.w > 0.5) {
        // Source-to-outlet is an authored explanatory scene with deliberately
        // broad dry banks. Its numerical heights are unchanged; this limited
        // height/slope cue only makes the valley, ridges, and constriction
        // legible from its longer home view.
        float demo_elevation = pow(smoothstep(0.55, 5.60, world_position.y), 1.35);
        float slope = 1.0 - normal.y;
        float slope_cue = smoothstep(0.002, 0.028, slope);
        float terrain_cue = max(0.20 * demo_elevation, 0.85 * slope_cue);
        vec3 demo_valley = cubey_srgb_to_linear(vec3(0.055, 0.160, 0.090));
        vec3 demo_ridge = cubey_srgb_to_linear(vec3(0.62, 0.43, 0.20));
        albedo = mix(demo_valley, demo_ridge, terrain_cue);
        albedo = mix(albedo, cubey_srgb_to_linear(vec3(0.36, 0.27, 0.12)),
                     0.24 * slope_cue);
    }
    if (params.terrain_palette.z > 0.5) {
        // The opt-in terrain-case palette range is expressed in physical
        // metres. Convert the rendered vertex height back to those metres so
        // changing vertical relief cannot also change the color mapping.
        float terrain_height_m = world_position.y / max(params.grid_cell.w, 1.0e-6);
        float palette_elevation = smoothstep(params.terrain_palette.x,
                                             params.terrain_palette.y,
                                             terrain_height_m);
        albedo = mix(lowland, highland, palette_elevation) + vec3(contour);
    }
    vec3 lighting = params.presentation.w > 1.5 ? vec3(0.42) + vec3(0.58) * diffuse
                                                 : vec3(0.26) + vec3(0.74) * diffuse;
    vec3 color = max(albedo * lighting, vec3(0.0));
    // Water Isolation is a presentation-only reading aid: make the bed quiet
    // and neutral so the water edge carries the visual contrast. Composite and
    // Flow Inspection intentionally retain the original terrain shading.
    if (params.presentation.y > 0.5 && params.presentation.y < 1.5) {
        float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
        color = mix(vec3(luminance), vec3(0.020, 0.028, 0.038), 0.55) * 0.42;
    }
    out_color = vec4(color, 1.0);
}
