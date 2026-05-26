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

void main() {
    vec2 screen_uv = frag_position * 0.5 + 0.5;
    vec2 uv = vec2(screen_uv.x, 1.0 - screen_uv.y);
    uint width = uint(params.grid_debug.x);
    uint height = uint(params.grid_debug.y);
    float solid_value = solid_cell_at(uv, width, height);

    vec3 background = vec3(0.008, 0.012, 0.018) + vec3(0.018, 0.026, 0.034) * uv.y;
    if (solid_value > 0.45) {
        out_color = vec4(cubey_srgb_to_linear(vec3(0.070, 0.082, 0.096)), 1.0);
        return;
    }

    vec2 texel_size = 1.0 / vec2(textureSize(surface_density, 0));
    float density = texture(surface_density, screen_uv).r;
    float left_density = texture(surface_density, screen_uv - vec2(texel_size.x, 0.0)).r;
    float right_density = texture(surface_density, screen_uv + vec2(texel_size.x, 0.0)).r;
    float down_density = texture(surface_density, screen_uv - vec2(0.0, texel_size.y)).r;
    float up_density = texture(surface_density, screen_uv + vec2(0.0, texel_size.y)).r;
    vec2 density_gradient = vec2(right_density - left_density, up_density - down_density);

    float threshold = max(0.05, params.surface_options.x);
    float edge_width = max(threshold * 0.08, fwidth(density) * 2.5);
    float water = smoothstep(threshold * 0.32, threshold + edge_width, density);
    float edge_distance = abs(density - threshold);
    float surface = (1.0 - smoothstep(edge_width * 0.65, edge_width * 3.2, edge_distance)) * water;
    float interior = smoothstep(threshold, threshold * 2.8, density);

    vec2 velocity = vec2(sample_u(uv, width, height), sample_v(uv, width, height));
    float speed = clamp(length(velocity) * 0.9, 0.0, 1.0);
    vec2 contour_gradient = density_gradient * mix(0.12, 1.0, surface);
    float gradient_strength = clamp(length(contour_gradient) * 18.0, 0.0, 1.0);
    float foam_signal = surface * smoothstep(0.18, 0.95, speed + gradient_strength * 0.45);
    float foam_noise = value_noise(uv * vec2(width, height) * 0.28);
    float foam_breakup = clamp(params.foam_options.y, 0.0, 1.0);
    float breakup_mask = mix(1.0, smoothstep(0.18, 0.82, foam_noise + speed * 0.20),
                             foam_breakup);
    float foam_sharpness = max(0.10, params.foam_options.x);
    float foam = pow(clamp(foam_signal * breakup_mask, 0.0, 1.0), foam_sharpness) *
                 params.surface_options.z;

    vec3 normal = normalize(vec3(-contour_gradient * params.surface_options.y * 28.0, 0.55));
    float light = clamp(dot(normal, normalize(vec3(-0.42, -0.72, 0.72))) * 0.5 + 0.5, 0.0, 1.0);
    vec3 water_color = mix(vec3(0.020, 0.125, 0.230), vec3(0.036, 0.235, 0.420), interior);
    water_color += vec3(0.012, 0.036, 0.052) * speed;
    water_color *= mix(0.82, 1.20, light);

    vec3 color = mix(background, water_color, water);
    color += vec3(0.52, 0.83, 0.98) * surface * params.surface_options.y;
    color += vec3(0.82, 0.94, 1.0) * foam;
    color += vec3(0.012, 0.035, 0.055) * smoothstep(0.0, 0.9, speed);
    out_color = vec4(cubey_srgb_to_linear(clamp(color, vec3(0.0), vec3(1.0))), 1.0);
}
