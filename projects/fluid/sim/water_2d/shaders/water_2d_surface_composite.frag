#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"
#include "cubey/procedural/noise.glsl"
#include "water_2d_contract.glsl"
#include "water_2d_grid.glsl"

WATER2D_RENDER_PARAMS;

layout(set = 0, binding = 0) uniform sampler2D surface_density;

layout(set = 1, binding = WATER2D_BINDING_U_FIELD, std430) readonly buffer UField {
    float values[];
} u_field;
layout(set = 1, binding = WATER2D_BINDING_V_FIELD, std430) readonly buffer VField {
    float values[];
} v_field;
layout(set = 1, binding = WATER2D_BINDING_SOLID, std430) readonly buffer SolidField {
    float values[];
} solid;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_color;

float solid_cell_at(vec2 uv, uint width, uint height) {
    ivec2 coord = clamp(ivec2(floor(uv * vec2(width, height))), ivec2(0),
                        ivec2(int(width) - 1, int(height) - 1));
    return solid.values[cell_index(coord, width, height)];
}

float sample_u(vec2 uv, uint width, uint height) {
    vec2 position = vec2(uv.x * float(width), uv.y * float(height) - 0.5);
    ivec2 base = ivec2(floor(position));
    vec2 f = fract(position);
    float a = u_field.values[u_index(base, width, height)];
    float b = u_field.values[u_index(base + ivec2(1, 0), width, height)];
    float c = u_field.values[u_index(base + ivec2(0, 1), width, height)];
    float d = u_field.values[u_index(base + ivec2(1, 1), width, height)];
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float sample_v(vec2 uv, uint width, uint height) {
    vec2 position = vec2(uv.x * float(width) - 0.5, uv.y * float(height));
    ivec2 base = ivec2(floor(position));
    vec2 f = fract(position);
    float a = v_field.values[v_index(base, width, height)];
    float b = v_field.values[v_index(base + ivec2(1, 0), width, height)];
    float c = v_field.values[v_index(base + ivec2(0, 1), width, height)];
    float d = v_field.values[v_index(base + ivec2(1, 1), width, height)];
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

vec3 scene_backdrop(vec2 solver_uv, uint appearance) {
    if (appearance == WATER2D_APPEARANCE_STRIPED_LIGHTBOX) {
        float axis = dot(solver_uv, vec2(0.91, 0.37));
        float cream = smoothstep(0.17, 0.174, axis) *
                      (1.0 - smoothstep(0.34, 0.344, axis));
        float brass = smoothstep(0.53, 0.534, axis) *
                      (1.0 - smoothstep(0.68, 0.684, axis));
        float high_cream = smoothstep(0.96, 0.964, axis) *
                           (1.0 - smoothstep(1.10, 1.104, axis));
        vec3 color = vec3(0.375, 0.333, 0.285);
        color = mix(color, vec3(0.775, 0.725, 0.638), max(cream, high_cream));
        color = mix(color, vec3(0.744, 0.561, 0.326), brass);
        return color;
    }

    float height_light = smoothstep(0.0, 0.92, solver_uv.y);
    vec3 color = mix(vec3(0.095, 0.133, 0.155), vec3(0.139, 0.171, 0.177),
                     height_light);
    float wall_light = 1.0 - smoothstep(0.12, 0.86,
                                      length((solver_uv - vec2(0.29, 0.56)) * vec2(1.0, 0.7)));
    color += vec3(0.038, 0.044, 0.038) * wall_light;

    vec2 guide = abs(fract(solver_uv * vec2(8.0, 5.0)) - 0.5);
    float grid_line = smoothstep(0.488, 0.5, max(guide.x, guide.y));
    return color + vec3(0.014, 0.018, 0.019) * grid_line;
}

vec3 paper_color(vec2 canvas, uint appearance) {
    float gradient = smoothstep(0.0, 1.12, canvas.x * 0.53 + (1.0 - canvas.y) * 0.70);
    if (appearance == WATER2D_APPEARANCE_DYE_RIBBON) {
        return mix(vec3(0.748, 0.730, 0.789), vec3(0.893, 0.875, 0.904), gradient);
    }
    return mix(vec3(0.894, 0.848, 0.744), vec3(0.954, 0.924, 0.844), gradient);
}

float caustic_pattern(vec2 uv, float time) {
    vec2 flow_a = uv + vec2(time * 0.18, -time * 0.10);
    vec2 flow_b = uv * 1.37 + vec2(-time * 0.12, time * 0.16);
    float ribbons = sin((flow_a.x * 1.35 + flow_a.y * 1.70) * 6.28318);
    ribbons += sin((flow_b.x * 2.15 - flow_b.y * 1.10) * 6.28318);
    ribbons += sin((flow_a.x * -1.65 + flow_b.y * 2.40 + time * 0.35) * 6.28318);
    float cells = cubey_proc_value_noise_pcg_2d(uv * 4.0 + vec2(time * 0.32, time * 0.21));
    float signal = 0.5 + ribbons * 0.13 + cells * 0.34;
    return smoothstep(0.58, 0.92, signal);
}

struct SurfaceState {
    float density;
    vec2 density_gradient;
    float curvature;
    float threshold;
    float edge_width;
    float water;
    float surface;
    float interior;
    float speed;
    float curvature_signal;
    float foam;
    float whitewater;
    float lighting;
    float specular;
    vec3 light_dir;
};

SurfaceState reconstruct_surface(vec2 screen_uv, vec2 uv, uint width, uint height,
                                 uint appearance) {
    SurfaceState state;
    vec2 texel_size = 1.0 / vec2(textureSize(surface_density, 0));
    state.density = texture(surface_density, screen_uv).r;
    float left_density = texture(surface_density, screen_uv - vec2(texel_size.x, 0.0)).r;
    float right_density = texture(surface_density, screen_uv + vec2(texel_size.x, 0.0)).r;
    float down_density = texture(surface_density, screen_uv - vec2(0.0, texel_size.y)).r;
    float up_density = texture(surface_density, screen_uv + vec2(0.0, texel_size.y)).r;
    state.density_gradient = vec2(right_density - left_density, up_density - down_density);
    state.curvature = abs((left_density + right_density + down_density + up_density) -
                          state.density * 4.0);

    state.threshold = max(0.05, params.surface_options.x);
    state.edge_width = max(state.threshold * 0.08, fwidth(state.density) * 2.5);
    state.water = smoothstep(state.threshold * 0.32, state.threshold + state.edge_width,
                             state.density);
    float edge_distance = abs(state.density - state.threshold);
    state.surface = (1.0 - smoothstep(state.edge_width * 0.65, state.edge_width * 3.2,
                                      edge_distance)) * state.water;
    state.interior = appearance == WATER2D_APPEARANCE_CLASSIC
                         ? smoothstep(state.threshold, state.threshold * 2.8, state.density)
                         : 0.0;

    float style_time = params.style_options.w;
    vec2 velocity = vec2(sample_u(uv, width, height), sample_v(uv, width, height));
    state.speed = clamp(length(velocity) * 0.9, 0.0, 1.0);
    vec2 contour_gradient = state.density_gradient * mix(0.12, 1.0, state.surface);
    float gradient_strength = clamp(length(contour_gradient) * 18.0, 0.0, 1.0);
    state.curvature_signal = smoothstep(0.010, 0.085,
                                        state.curvature * 3.8 + state.speed * 0.12 +
                                            gradient_strength * 0.08);
    float foam_signal =
        state.surface *
        smoothstep(0.38, 1.16, state.speed * 0.78 + state.curvature_signal * 0.86 +
                                  gradient_strength * 0.12);
    float foam_noise =
        cubey_proc_value_noise_pcg_2d(uv * vec2(width, height) * 0.22 +
                                      vec2(style_time * 0.24, -style_time * 0.18));
    float foam_breakup = clamp(params.foam_options.y, 0.0, 1.0);
    float breakup_mask = mix(1.0, smoothstep(0.22, 0.82, foam_noise + state.speed * 0.20 +
                                                                  state.curvature_signal * 0.16),
                             foam_breakup);
    float foam_sharpness = max(0.10, params.foam_options.x);
    state.foam = pow(clamp(foam_signal * breakup_mask, 0.0, 1.0), foam_sharpness) *
                 params.surface_options.z;
    float whitewater_noise =
        cubey_proc_value_noise_pcg_2d(uv * vec2(width, height) * 0.46 +
                                      vec2(-style_time * 0.18, style_time * 0.30));
    float whitewater_signal =
        state.surface *
        smoothstep(0.50, 1.20, state.speed * 0.86 + state.curvature_signal * 0.95 +
                                  gradient_strength * 0.16);
    float whitewater_mask =
        mix(1.0, smoothstep(0.24, 0.86, whitewater_noise + state.curvature_signal * 0.28),
            foam_breakup);
    state.whitewater =
        pow(clamp(whitewater_signal * whitewater_mask, 0.0, 1.0),
            max(0.35, foam_sharpness * 0.72)) *
        params.surface_options.z;

    state.lighting = 0.0;
    state.specular = 0.0;
    state.light_dir = normalize(vec3(-0.38, -0.58, 0.72));
    if (appearance == WATER2D_APPEARANCE_CLASSIC ||
        appearance == WATER2D_APPEARANCE_STRIPED_LIGHTBOX) {
        vec3 normal = normalize(vec3(-contour_gradient * params.surface_options.y * 28.0, 0.55));
        if (appearance == WATER2D_APPEARANCE_CLASSIC) {
            state.lighting = clamp(dot(normal, state.light_dir) * 0.5 + 0.5, 0.0, 1.0);
        }
        vec3 half_dir = normalize(state.light_dir + vec3(0.0, 0.0, 1.0));
        state.specular = pow(max(dot(normal, half_dir), 0.0), 54.0) * state.surface *
                         max(0.0, params.style_options.z);
    }
    return state;
}

vec3 classic_material(SurfaceState state, vec2 screen_uv, vec2 uv, uint width, uint height) {
    float refraction_strength = max(0.0, params.style_options.x);
    vec2 refraction_offset = vec2(state.density_gradient.x, -state.density_gradient.y) *
                             refraction_strength * state.water;
    vec2 refracted_screen_uv = clamp(screen_uv + refraction_offset, vec2(0.0), vec2(1.0));
    vec2 refracted_solver_uv = vec2(refracted_screen_uv.x, 1.0 - refracted_screen_uv.y);
    vec3 background = scene_backdrop(refracted_solver_uv, WATER2D_APPEARANCE_CLASSIC);

    // Density estimates screen coverage. The broad light gradient shades the
    // tank from above and gives the water body tonal separation from the crest.
    float optical_density = max(0.0, state.density - state.threshold * 0.48);
    float absorption = 1.0 - exp(-optical_density * 0.34);
    float thickness = smoothstep(state.threshold * 0.45, state.threshold * 3.8, state.density);
    vec3 shallow_water = vec3(0.242, 0.731, 0.726);
    vec3 deep_water = vec3(0.056, 0.401, 0.479);
    float tank_light = smoothstep(0.0, 0.62, uv.y + (0.48 - uv.x) * 0.10);
    vec3 water_color = mix(shallow_water, deep_water, absorption);
    water_color *= mix(vec3(0.88, 0.88, 0.94), vec3(1.12, 1.16, 1.12), tank_light);
    water_color *= mix(0.94, 1.10, state.lighting);
    water_color += vec3(0.014, 0.023, 0.019) * state.speed;
    water_color += vec3(0.0, 0.100, 0.129) * state.interior;

    // The density just above this pixel gives the light below an exposed
    // waterline a soft falloff. The reconstructed coverage stays the same.
    float near_density = texture(surface_density, screen_uv - vec2(0.0, 0.019)).r;
    float upper_density = texture(surface_density, screen_uv - vec2(0.0, 0.046)).r;
    float coverage_above =
        0.68 * smoothstep(state.threshold * 0.23, state.threshold * 3.0, near_density) +
        0.32 * smoothstep(state.threshold * 0.20, state.threshold * 3.6, upper_density);
    float surface_light = (1.0 - coverage_above) *
                          smoothstep(state.threshold * 0.27, state.threshold * 2.1,
                                     state.density);
    water_color += vec3(0.054, 0.104, 0.086) * surface_light;

    float style_time = params.style_options.w;
    float caustics = caustic_pattern((uv + state.density_gradient * 0.10) *
                                         vec2(width, height) * 0.026,
                                     style_time);
    water_color += vec3(0.068, 0.147, 0.112) * caustics *
                   max(0.0, params.style_options.y) * (1.0 - state.interior * 0.50);
    float optical_opacity = state.water * mix(0.28, 0.72, thickness);
    vec3 color = mix(background, water_color, optical_opacity);

    // The outward density gradient makes the reflection directional. Use
    // one bounded foam blend to avoid whitening the same contour twice.
    vec2 interface_normal = -state.density_gradient /
                            max(length(state.density_gradient), 0.0001);
    float facing_light = smoothstep(-0.25, 0.85,
                                   dot(interface_normal, normalize(state.light_dir.xy)));
    float rim = 0.59 * params.surface_options.y * state.surface *
                mix(0.24, 1.0, facing_light);
    float reflection = clamp(rim + state.specular * 0.43, 0.0, 0.55);
    color = mix(color, vec3(0.64, 0.88, 0.86), reflection);

    float foam_amount = max(state.foam * 0.78, state.whitewater);
    float foamy_impact = (1.0 - exp(-foam_amount * 2.0)) *
                         smoothstep(0.11, 0.44, state.speed);
    color = mix(color, vec3(0.86, 0.95, 0.95), clamp(foamy_impact, 0.0, 0.82));
    return color;
}

vec3 lightbox_material(SurfaceState state, vec2 uv) {
    vec3 dry = scene_backdrop(uv, WATER2D_APPEARANCE_STRIPED_LIGHTBOX);
    float refraction_scale = max(0.0, params.style_options.x) / 0.022;
    vec2 broad_shift = vec2(0.060, -0.022) * refraction_scale *
                       smoothstep(state.threshold * 0.38, state.threshold * 1.65,
                                  state.density);
    vec2 edge_shift = clamp(vec2(state.density_gradient.x, -state.density_gradient.y) * 0.18,
                            vec2(-0.046), vec2(0.046)) * refraction_scale;
    vec3 bent = scene_backdrop(clamp(uv + broad_shift + edge_shift, vec2(0.0), vec2(1.0)),
                               WATER2D_APPEARANCE_STRIPED_LIGHTBOX);
    vec3 transmitted = mix(bent * vec3(0.62, 0.90, 0.98), vec3(0.20, 0.52, 0.57), 0.31);
    vec3 color = mix(dry, transmitted, state.water);
    vec2 outward = -state.density_gradient / max(length(state.density_gradient), 0.0001);
    float facing = smoothstep(0.02, 0.89, dot(outward, normalize(state.light_dir.xy)));
    float rim = state.surface * mix(0.11, 0.39, facing) + state.specular * 0.34;
    float foam_amount = max(state.foam, state.whitewater) *
                        smoothstep(0.18, 0.65, state.speed);
    return mix(color, vec3(0.74, 0.91, 0.88), clamp(rim + foam_amount * 0.74, 0.0, 0.72));
}

vec3 relief_material(SurfaceState state, vec3 paper, vec2 screen_uv) {
    vec3 aqueous_ink = vec3(0.065, 0.128, 0.258);
    vec3 top_ink = vec3(0.125, 0.278, 0.407);
    float upper_density = texture(surface_density, screen_uv - vec2(0.0, 0.041)).r;
    float nearby_surface = 1.0 - smoothstep(state.threshold * 0.26,
                                             state.threshold * 1.72, upper_density);
    vec3 ink = mix(aqueous_ink, top_ink, nearby_surface);
    float foam_amount = max(state.foam, state.whitewater) *
                        smoothstep(0.15, 0.54, state.speed);
    float exposed_lip = state.surface * smoothstep(0.20, 0.73, state.speed) *
                        smoothstep(0.42, 0.80, state.curvature_signal);
    ink = mix(ink, vec3(0.76, 0.224, 0.119), clamp(exposed_lip * 0.87, 0.0, 0.85));
    ink = mix(ink, paper, clamp(foam_amount * 0.48, 0.0, 0.20));
    float coverage = smoothstep(state.threshold * 0.42,
                                state.threshold + state.edge_width * 0.62, state.density);
    return mix(paper, ink, coverage);
}

vec3 dye_material(SurfaceState state, vec3 paper, vec2 screen_uv, vec2 uv) {
    vec2 surface_channels = texture(surface_density, screen_uv).rg;
    float dye_fraction = clamp(surface_channels.g / max(surface_channels.r, 0.0001), 0.0, 1.0);
    float depth_tone = smoothstep(0.0, 0.52, uv.y);
    vec3 body = mix(vec3(0.201, 0.513, 0.471), vec3(0.493, 0.717, 0.611), depth_tone);
    vec3 plum = mix(vec3(0.272, 0.105, 0.270), vec3(0.517, 0.218, 0.371), depth_tone);
    vec3 tint = mix(body, plum, dye_fraction);
    float foam_amount = max(state.foam, state.whitewater) *
                        smoothstep(0.15, 0.57, state.speed);
    float upper_edge = state.surface *
                       smoothstep(-0.22, 0.8, -state.density_gradient.y /
                                                   max(length(state.density_gradient), 0.0001));
    tint = mix(tint, vec3(0.86, 0.89, 0.76),
               clamp(0.20 * upper_edge + foam_amount * 0.9, 0.0, 0.5));
    return mix(paper, tint, state.water * 0.98);
}

void main() {
    uint appearance = params.appearance_options.x;
    if (appearance > WATER2D_APPEARANCE_DYE_RIBBON) {
        appearance = WATER2D_APPEARANCE_CLASSIC;
    }

    vec2 canvas = frag_position * 0.5 + 0.5;
    vec2 screen_uv = canvas;
    if (appearance == WATER2D_APPEARANCE_STRIPED_LIGHTBOX) {
        screen_uv = (canvas - vec2(0.055, 0.055)) / 0.89;
    } else if (appearance == WATER2D_APPEARANCE_RELIEF_PRINT ||
               appearance == WATER2D_APPEARANCE_DYE_RIBBON) {
        screen_uv = (canvas - vec2(0.063, 0.063)) / 0.874;
    }
    vec2 uv = vec2(screen_uv.x, 1.0 - screen_uv.y);
    uint width = uint(params.grid_debug.x);
    uint height = uint(params.grid_debug.y);

    vec3 paper = vec3(0.0);
    if (appearance == WATER2D_APPEARANCE_RELIEF_PRINT ||
        appearance == WATER2D_APPEARANCE_DYE_RIBBON) {
        paper = paper_color(canvas, appearance);
    }
    if (appearance != WATER2D_APPEARANCE_CLASSIC &&
        min(min(screen_uv.x, 1.0 - screen_uv.x), min(screen_uv.y, 1.0 - screen_uv.y)) < 0.0) {
        vec3 border = appearance == WATER2D_APPEARANCE_STRIPED_LIGHTBOX
                          ? vec3(0.156, 0.163, 0.161)
                          : paper;
        out_color = vec4(cubey_srgb_to_linear(border), 1.0);
        return;
    }

    float solid_value = solid_cell_at(uv, width, height);
    if (solid_value > 0.45) {
        if (appearance == WATER2D_APPEARANCE_CLASSIC) {
            vec3 solid_color = scene_backdrop(uv, appearance);
            vec2 wall_distance = vec2(min(uv.x, 1.0 - uv.x), uv.y) * vec2(width, height);
            vec2 inner_edge = smoothstep(vec2(1.0), vec2(2.0), wall_distance) *
                              (1.0 - step(vec2(2.0), wall_distance));
            solid_color = mix(solid_color, vec3(0.151, 0.185, 0.201), 0.48);
            solid_color += vec3(0.050, 0.053, 0.051) * max(inner_edge.x, inner_edge.y);
            out_color = vec4(cubey_srgb_to_linear(solid_color), 1.0);
        } else if (appearance == WATER2D_APPEARANCE_STRIPED_LIGHTBOX) {
            out_color = vec4(cubey_srgb_to_linear(vec3(0.325, 0.321, 0.294)), 1.0);
        } else if (appearance == WATER2D_APPEARANCE_RELIEF_PRINT) {
            vec3 ink_line = mix(paper, vec3(0.12, 0.23, 0.31), 0.47);
            out_color = vec4(cubey_srgb_to_linear(ink_line), 1.0);
        } else {
            vec3 ink_line = mix(paper, vec3(0.243, 0.181, 0.292), 0.55);
            out_color = vec4(cubey_srgb_to_linear(ink_line), 1.0);
        }
        return;
    }

    SurfaceState state = reconstruct_surface(screen_uv, uv, width, height, appearance);
    vec3 color;
    if (appearance == WATER2D_APPEARANCE_CLASSIC) {
        color = classic_material(state, screen_uv, uv, width, height);
    } else if (appearance == WATER2D_APPEARANCE_STRIPED_LIGHTBOX) {
        color = lightbox_material(state, uv);
    } else if (appearance == WATER2D_APPEARANCE_RELIEF_PRINT) {
        color = relief_material(state, paper, screen_uv);
    } else {
        color = dye_material(state, paper, screen_uv, uv);
    }
    out_color = vec4(cubey_srgb_to_linear(clamp(color, vec3(0.0), vec3(1.0))), 1.0);
}
