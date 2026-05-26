#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"
#include "water_2d_contract.glsl"

WATER2D_RENDER_PARAMS;

layout(set = 0, binding = WATER2D_BINDING_PARTICLE_POSITIONS, std430) readonly buffer ParticlePositions {
    vec4 values[];
} particle_positions;
layout(set = 0, binding = WATER2D_BINDING_U_FIELD, std430) readonly buffer UField {
    float values[];
} u_field;
layout(set = 0, binding = WATER2D_BINDING_V_FIELD, std430) readonly buffer VField {
    float values[];
} v_field;
layout(set = 0, binding = WATER2D_BINDING_PRESSURE_A, std430) readonly buffer PressureAField {
    float values[];
} pressure_a;
layout(set = 0, binding = WATER2D_BINDING_PRESSURE_B, std430) readonly buffer PressureBField {
    float values[];
} pressure_b;
layout(set = 0, binding = WATER2D_BINDING_DIVERGENCE, std430) readonly buffer DivergenceField {
    float values[];
} divergence;
layout(set = 0, binding = WATER2D_BINDING_SOLID, std430) readonly buffer SolidField {
    float values[];
} solid;
layout(set = 0, binding = WATER2D_BINDING_CELL_COUNTS, std430) readonly buffer CellCounts {
    uint values[];
} cell_counts;
layout(set = 0, binding = WATER2D_BINDING_CELL_PARTICLE_INDICES, std430) readonly buffer CellParticleIndices {
    uint values[];
} cell_particle_indices;

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

float sample_cell_field(vec2 uv, uint width, uint height, int field_id) {
    vec2 position = uv * vec2(width, height) - vec2(0.5);
    ivec2 base = ivec2(floor(position));
    vec2 f = fract(position);
    uint ia = cell_index(base, width, height);
    uint ib = cell_index(base + ivec2(1, 0), width, height);
    uint ic = cell_index(base + ivec2(0, 1), width, height);
    uint id = cell_index(base + ivec2(1, 1), width, height);

    float a = 0.0;
    float b = 0.0;
    float c = 0.0;
    float d = 0.0;
    if (field_id == 0) {
        a = float(cell_counts.values[ia]);
        b = float(cell_counts.values[ib]);
        c = float(cell_counts.values[ic]);
        d = float(cell_counts.values[id]);
    } else if (field_id == 1) {
        bool use_pressure_b = params.grid_debug.w > 0.5;
        a = use_pressure_b ? pressure_b.values[ia] : pressure_a.values[ia];
        b = use_pressure_b ? pressure_b.values[ib] : pressure_a.values[ib];
        c = use_pressure_b ? pressure_b.values[ic] : pressure_a.values[ic];
        d = use_pressure_b ? pressure_b.values[id] : pressure_a.values[id];
    } else if (field_id == 2) {
        a = divergence.values[ia];
        b = divergence.values[ib];
        c = divergence.values[ic];
        d = divergence.values[id];
    } else {
        a = solid.values[ia];
        b = solid.values[ib];
        c = solid.values[ic];
        d = solid.values[id];
    }
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
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

float particle_density(vec2 uv, uint width, uint height, float radius, float aspect,
                       uint max_particles_per_cell) {
    ivec2 center = ivec2(floor(uv * vec2(width, height)));
    int support = clamp(int(ceil(radius * float(height))) + 1, 1, 4);
    float density = 0.0;
    for (int oy = -4; oy <= 4; ++oy) {
        if (abs(oy) > support) {
            continue;
        }
        for (int ox = -4; ox <= 4; ++ox) {
            if (abs(ox) > support) {
                continue;
            }
            ivec2 cell = center + ivec2(ox, oy);
            if (cell.x < 0 || cell.y < 0 || cell.x >= int(width) || cell.y >= int(height)) {
                continue;
            }
            uint cell_id = cell_index(cell, width, height);
            uint count = min(cell_counts.values[cell_id], max_particles_per_cell);
            for (uint slot = 0u; slot < count; ++slot) {
                uint particle_id =
                    cell_particle_indices.values[(cell_id * max_particles_per_cell) + slot];
                if (particle_id == WATER2D_EMPTY_PARTICLE) {
                    continue;
                }
                vec2 delta = (particle_positions.values[particle_id].xy - uv) * vec2(aspect, 1.0);
                float distance = length(delta);
                density += smoothstep(radius, 0.0, distance);
            }
        }
    }
    return density;
}

vec3 particle_surface_sample(vec2 uv, uint width, uint height, float radius, float aspect,
                             uint max_particles_per_cell) {
    ivec2 center = ivec2(floor(uv * vec2(width, height)));
    int support = clamp(int(ceil(radius * float(height))) + 1, 1, 4);
    float density = 0.0;
    vec2 gradient = vec2(0.0);
    for (int oy = -4; oy <= 4; ++oy) {
        if (abs(oy) > support) {
            continue;
        }
        for (int ox = -4; ox <= 4; ++ox) {
            if (abs(ox) > support) {
                continue;
            }
            ivec2 cell = center + ivec2(ox, oy);
            if (cell.x < 0 || cell.y < 0 || cell.x >= int(width) || cell.y >= int(height)) {
                continue;
            }
            uint cell_id = cell_index(cell, width, height);
            uint count = min(cell_counts.values[cell_id], max_particles_per_cell);
            for (uint slot = 0u; slot < count; ++slot) {
                uint particle_id =
                    cell_particle_indices.values[(cell_id * max_particles_per_cell) + slot];
                if (particle_id == WATER2D_EMPTY_PARTICLE) {
                    continue;
                }
                vec2 delta = (particle_positions.values[particle_id].xy - uv) * vec2(aspect, 1.0);
                float distance = length(delta);
                float contribution = smoothstep(radius, 0.0, distance);
                density += contribution;
                if (distance > 0.00001 && distance < radius) {
                    float slope = 6.0 * (distance / radius) * (1.0 - (distance / radius));
                    gradient -= normalize(delta) * slope;
                }
            }
        }
    }
    return vec3(density, gradient);
}

vec3 signed_scalar_color(float value, float scale) {
    float positive = clamp(value * scale, 0.0, 1.0);
    float negative = clamp(-value * scale, 0.0, 1.0);
    return vec3(0.075, 0.085, 0.105) + vec3(0.92, 0.22, 0.10) * positive +
           vec3(0.10, 0.42, 0.92) * negative;
}

void main() {
    vec2 screen_uv = frag_position * 0.5 + 0.5;
    vec2 uv = vec2(screen_uv.x, 1.0 - screen_uv.y);
    uint width = uint(params.grid_debug.x);
    uint height = uint(params.grid_debug.y);
    int debug_mode = int(params.grid_debug.z + 0.5);
    uint max_particles_per_cell = uint(params.particle_options.y + 0.5);
    float radius = params.particle_options.z;
    float aspect = float(width) / float(height);
    float solid_value = solid_cell_at(uv, width, height);
    vec2 velocity = vec2(sample_u(uv, width, height), sample_v(uv, width, height));
    float speed = clamp(length(velocity) * 0.9, 0.0, 1.0);

    if (debug_mode == 1) {
        float density = particle_density(uv, width, height, radius * 0.72, aspect,
                                         max_particles_per_cell);
        vec3 color = mix(vec3(0.014, 0.018, 0.024), vec3(0.38, 0.74, 0.98),
                         clamp(density, 0.0, 1.0));
        out_color = vec4(cubey_srgb_to_linear(color), 1.0);
        return;
    }
    if (debug_mode == 2) {
        float occupancy = clamp(sample_cell_field(uv, width, height, 0) / 4.0, 0.0, 1.0);
        vec3 color = mix(vec3(0.025, 0.030, 0.038), vec3(0.08, 0.46, 0.78), occupancy);
        if (solid_value > 0.5) {
            color = vec3(0.78, 0.82, 0.88);
        }
        out_color = vec4(cubey_srgb_to_linear(color), 1.0);
        return;
    }
    if (debug_mode == 3) {
        vec2 direction = speed > 0.001 ? normalize(velocity) : vec2(0.0);
        vec2 screen_direction = vec2(direction.x, -direction.y);
        vec3 vector_color = vec3(screen_direction * 0.35 + 0.5, 0.92);
        vec3 neutral = vec3(0.045, 0.055, 0.070);
        out_color = vec4(cubey_srgb_to_linear(mix(neutral, vector_color, speed)), 1.0);
        return;
    }
    if (debug_mode == 4) {
        float value = sample_cell_field(uv, width, height, 2);
        out_color = vec4(cubey_srgb_to_linear(signed_scalar_color(value, 30.0)), 1.0);
        return;
    }
    if (debug_mode == 5) {
        float value = sample_cell_field(uv, width, height, 1);
        out_color = vec4(cubey_srgb_to_linear(signed_scalar_color(value, 5.0)), 1.0);
        return;
    }
    if (debug_mode == 6) {
        out_color = vec4(cubey_srgb_to_linear(mix(vec3(0.04, 0.045, 0.055),
                                                  vec3(0.82, 0.86, 0.90),
                                                  clamp(solid_value, 0.0, 1.0))),
                         1.0);
        return;
    }

    vec3 background = vec3(0.008, 0.012, 0.018) + vec3(0.018, 0.026, 0.034) * uv.y;
    if (solid_value > 0.45) {
        vec3 tank_color = vec3(0.070, 0.082, 0.096);
        out_color = vec4(cubey_srgb_to_linear(tank_color), 1.0);
        return;
    }

    float threshold = max(0.08, params.surface_options.x);
    vec3 surface_sample =
        particle_surface_sample(uv, width, height, radius, aspect, max_particles_per_cell);
    float density = surface_sample.x;
    vec2 density_gradient = surface_sample.yz;
    float gradient_strength = clamp(length(density_gradient) * 0.55, 0.0, 1.0);
    float water = smoothstep(threshold * 0.06, threshold, density);
    float surface = smoothstep(threshold * 0.16, threshold * 0.68, density) *
                    (1.0 - smoothstep(threshold * 0.76, threshold * 1.70, density));
    float foam = surface * smoothstep(0.20, 0.95, speed + gradient_strength * 0.50) *
                 params.surface_options.z;

    if (debug_mode == 7) {
        vec3 foam_color = mix(vec3(0.012, 0.016, 0.022), vec3(0.82, 0.93, 1.0),
                              clamp(foam, 0.0, 1.0));
        out_color = vec4(cubey_srgb_to_linear(foam_color), 1.0);
        return;
    }

    vec3 normal = normalize(vec3(-density_gradient * params.surface_options.y, 0.42));
    float light = clamp(dot(normal, normalize(vec3(-0.42, 0.72, 0.72))) * 0.5 + 0.5, 0.0, 1.0);
    vec3 water_color = mix(vec3(0.020, 0.120, 0.230), vec3(0.040, 0.300, 0.500),
                           clamp(density * 0.34, 0.0, 1.0));
    water_color += vec3(0.018, 0.050, 0.070) * speed;
    water_color *= mix(0.82, 1.20, light);
    vec3 color = mix(background, water_color, water);
    color += vec3(0.50, 0.80, 0.96) * surface * params.surface_options.y;
    color += vec3(0.82, 0.94, 1.0) * foam;
    color += vec3(0.012, 0.035, 0.055) * smoothstep(0.0, 0.9, speed);
    out_color = vec4(cubey_srgb_to_linear(clamp(color, vec3(0.0), vec3(1.0))), 1.0);
}
