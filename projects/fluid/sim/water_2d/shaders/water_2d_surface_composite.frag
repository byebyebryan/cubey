#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"
#include "water_2d_contract.glsl"

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

uint cell_index(ivec2 coord, uint width, uint height) {
    ivec2 clamped = clamp(coord, ivec2(0), ivec2(int(width) - 1, int(height) - 1));
    return (uint(clamped.y) * width) + uint(clamped.x);
}

uint u_index(ivec2 coord, uint width, uint height) {
    ivec2 clamped = ivec2(clamp(coord.x, 0, int(width)), clamp(coord.y, 0, int(height) - 1));
    return (uint(clamped.y) * (width + 1u)) + uint(clamped.x);
}

uint v_index(ivec2 coord, uint width, uint height) {
    ivec2 clamped = ivec2(clamp(coord.x, 0, int(width) - 1), clamp(coord.y, 0, int(height)));
    return (uint(clamped.y) * width) + uint(clamped.x);
}

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

float hash21(vec2 value) {
    vec3 p3 = fract(vec3(value.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float value_noise(vec2 uv) {
    vec2 base = floor(uv);
    vec2 f = fract(uv);
    vec2 shaped = f * f * (3.0 - 2.0 * f);
    float a = hash21(base);
    float b = hash21(base + vec2(1.0, 0.0));
    float c = hash21(base + vec2(0.0, 1.0));
    float d = hash21(base + vec2(1.0, 1.0));
    return mix(mix(a, b, shaped.x), mix(c, d, shaped.x), shaped.y);
}

vec3 scene_backdrop(vec2 screen_uv, vec2 solver_uv, uint width, uint height) {
    vec3 wall_bottom = vec3(0.048, 0.054, 0.061);
    vec3 wall_top = vec3(0.104, 0.114, 0.126);
    vec3 color = mix(wall_bottom, wall_top, screen_uv.y);

    float floor_band = 1.0 - smoothstep(0.00, 0.18, screen_uv.y);
    color = mix(color, vec3(0.058, 0.064, 0.069), floor_band * 0.75);

    float left_wall = 1.0 - smoothstep(0.00, 0.035, screen_uv.x);
    float right_wall = smoothstep(0.965, 1.00, screen_uv.x);
    float floor_line = 1.0 - smoothstep(0.00, 0.040, screen_uv.y);
    float wall_line = max(max(left_wall, right_wall), floor_line);
    color = mix(color, vec3(0.118, 0.128, 0.136), wall_line * 0.60);

    vec2 major = abs(fract(solver_uv * vec2(8.0, 6.0)) - 0.5);
    float guide = 1.0 - smoothstep(0.470, 0.500, max(major.x, major.y));
    color -= vec3(0.010, 0.011, 0.012) * guide * (1.0 - floor_band * 0.45);

    float cell_ratio = max(float(width), float(height)) / max(1.0, min(float(width), float(height)));
    color += vec3(0.010, 0.013, 0.015) * smoothstep(0.9, 1.8, cell_ratio) * screen_uv.y;
    return color;
}

float caustic_pattern(vec2 uv, float time) {
    vec2 flow_a = uv + vec2(time * 0.18, -time * 0.10);
    vec2 flow_b = uv * 1.37 + vec2(-time * 0.12, time * 0.16);
    float ribbons = sin((flow_a.x * 1.35 + flow_a.y * 1.70) * 6.28318);
    ribbons += sin((flow_b.x * 2.15 - flow_b.y * 1.10) * 6.28318);
    ribbons += sin((flow_a.x * -1.65 + flow_b.y * 2.40 + time * 0.35) * 6.28318);
    float cells = value_noise(uv * 4.0 + vec2(time * 0.32, time * 0.21));
    float signal = 0.5 + ribbons * 0.13 + cells * 0.34;
    return smoothstep(0.58, 0.92, signal);
}

void main() {
    vec2 screen_uv = frag_position * 0.5 + 0.5;
    vec2 uv = vec2(screen_uv.x, 1.0 - screen_uv.y);
    uint width = uint(params.grid_debug.x);
    uint height = uint(params.grid_debug.y);
    float solid_value = solid_cell_at(uv, width, height);

    if (solid_value > 0.45) {
        vec3 solid_color = scene_backdrop(screen_uv, uv, width, height);
        solid_color = mix(solid_color, vec3(0.105, 0.111, 0.116), 0.72);
        out_color = vec4(cubey_srgb_to_linear(solid_color), 1.0);
        return;
    }

    vec2 texel_size = 1.0 / vec2(textureSize(surface_density, 0));
    float density = texture(surface_density, screen_uv).r;
    float left_density = texture(surface_density, screen_uv - vec2(texel_size.x, 0.0)).r;
    float right_density = texture(surface_density, screen_uv + vec2(texel_size.x, 0.0)).r;
    float down_density = texture(surface_density, screen_uv - vec2(0.0, texel_size.y)).r;
    float up_density = texture(surface_density, screen_uv + vec2(0.0, texel_size.y)).r;
    vec2 density_gradient = vec2(right_density - left_density, up_density - down_density);
    float curvature = abs((left_density + right_density + down_density + up_density) -
                          density * 4.0);

    float threshold = max(0.05, params.surface_options.x);
    float edge_width = max(threshold * 0.08, fwidth(density) * 2.5);
    float water = smoothstep(threshold * 0.32, threshold + edge_width, density);
    float edge_distance = abs(density - threshold);
    float surface = (1.0 - smoothstep(edge_width * 0.65, edge_width * 3.2, edge_distance)) * water;
    float interior = smoothstep(threshold, threshold * 2.8, density);
    float refraction_strength = max(0.0, params.style_options.x);
    float caustic_strength = max(0.0, params.style_options.y);
    float specular_strength = max(0.0, params.style_options.z);
    float style_time = params.style_options.w;
    vec2 refraction_offset = vec2(density_gradient.x, -density_gradient.y) *
                             refraction_strength * water;
    vec2 refracted_screen_uv = clamp(screen_uv + refraction_offset, vec2(0.0), vec2(1.0));
    vec2 refracted_solver_uv = vec2(refracted_screen_uv.x, 1.0 - refracted_screen_uv.y);
    vec3 background = scene_backdrop(refracted_screen_uv, refracted_solver_uv, width, height);

    vec2 velocity = vec2(sample_u(uv, width, height), sample_v(uv, width, height));
    float speed = clamp(length(velocity) * 0.9, 0.0, 1.0);
    vec2 contour_gradient = density_gradient * mix(0.12, 1.0, surface);
    float gradient_strength = clamp(length(contour_gradient) * 18.0, 0.0, 1.0);
    float curvature_signal =
        smoothstep(0.010, 0.085, curvature * 3.8 + speed * 0.12 + gradient_strength * 0.08);
    float foam_signal =
        surface * smoothstep(0.18, 0.95, speed * 0.65 + curvature_signal + gradient_strength * 0.25);
    float foam_noise =
        value_noise(uv * vec2(width, height) * 0.22 + vec2(style_time * 0.24, -style_time * 0.18));
    float foam_breakup = clamp(params.foam_options.y, 0.0, 1.0);
    float breakup_mask = mix(1.0, smoothstep(0.22, 0.82,
                                             foam_noise + speed * 0.20 + curvature_signal * 0.16),
                             foam_breakup);
    float foam_sharpness = max(0.10, params.foam_options.x);
    float foam = pow(clamp(foam_signal * breakup_mask, 0.0, 1.0), foam_sharpness) *
                 params.surface_options.z;

    vec3 normal = normalize(vec3(-contour_gradient * params.surface_options.y * 28.0, 0.55));
    vec3 light_dir = normalize(vec3(-0.38, -0.58, 0.72));
    float light = clamp(dot(normal, light_dir) * 0.5 + 0.5, 0.0, 1.0);
    vec3 half_dir = normalize(light_dir + vec3(0.0, 0.0, 1.0));
    float specular = pow(max(dot(normal, half_dir), 0.0), 54.0) * surface * specular_strength;

    float thickness = clamp(density / max(threshold * 3.2, 0.001), 0.0, 1.0);
    float absorption = 1.0 - exp(-density * 0.85);
    vec3 shallow_water = vec3(0.075, 0.255, 0.360);
    vec3 deep_water = vec3(0.020, 0.105, 0.200);
    vec3 water_color = mix(shallow_water, deep_water, absorption);
    water_color += vec3(0.010, 0.032, 0.045) * speed;
    water_color *= mix(0.78, 1.18, light);

    float caustics = caustic_pattern((uv + density_gradient * 0.10) * vec2(width, height) * 0.026,
                                     style_time);
    background += vec3(0.070, 0.135, 0.155) * caustics * caustic_strength * water *
                  (1.0 - interior * 0.55);

    vec3 refracted_water = mix(background, water_color, mix(0.42, 0.82, thickness));
    vec3 color = mix(background, refracted_water, water);
    color += vec3(0.35, 0.62, 0.76) * surface * params.surface_options.y * (1.0 - foam * 0.55);
    color += vec3(0.86, 0.94, 1.0) * foam * 0.56;
    color += vec3(0.95, 0.98, 1.0) * specular;
    color += vec3(0.008, 0.026, 0.040) * smoothstep(0.0, 0.9, speed);
    out_color = vec4(cubey_srgb_to_linear(clamp(color, vec3(0.0), vec3(1.0))), 1.0);
}
