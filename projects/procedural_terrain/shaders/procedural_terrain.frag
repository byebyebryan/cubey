#version 450

layout(push_constant) uniform TerrainPushConstants {
    mat4 view_projection;
    vec4 light_direction_debug;
    vec4 field_ranges;
} pc;

layout(location = 0) in vec3 frag_world_position;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec4 frag_material;
layout(location = 3) in vec4 frag_fields;
layout(location = 4) in vec4 frag_generator;

layout(location = 0) out vec4 out_color;

const uint TERRAIN_VIEW_FINAL = 0u;
const uint TERRAIN_VIEW_HEIGHT = 1u;
const uint TERRAIN_VIEW_WATER_DEPTH = 2u;
const uint TERRAIN_VIEW_SHORELINE = 3u;
const uint TERRAIN_VIEW_MATERIAL = 4u;
const uint TERRAIN_VIEW_SLOPE = 5u;
const uint TERRAIN_VIEW_LANDFORM = 6u;
const uint TERRAIN_VIEW_RIDGES = 7u;
const uint TERRAIN_VIEW_VALLEYS = 8u;

vec3 ramp3(float t, vec3 a, vec3 b, vec3 c) {
    t = clamp(t, 0.0, 1.0);
    return t < 0.5 ? mix(a, b, t * 2.0) : mix(b, c, (t - 0.5) * 2.0);
}

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec3 material_color(vec4 material) {
    vec3 sand = vec3(0.74, 0.63, 0.43);
    vec3 rock = vec3(0.38, 0.39, 0.37);
    vec3 vegetation = vec3(0.18, 0.39, 0.22);
    vec3 sediment = vec3(0.24, 0.26, 0.23);
    return sand * material.r + rock * material.g + vegetation * material.b + sediment * material.a;
}

vec3 water_surface_color() {
    float depth_t = smoothstep(2.0, 76.0, frag_fields.y);
    float shore_t = 1.0 - smoothstep(2.0, 24.0, abs(frag_fields.z));
    float ripple = sin((frag_world_position.x * 0.010) + (frag_world_position.z * 0.014)) +
                   (sin((frag_world_position.x * 0.023) - (frag_world_position.z * 0.018)) * 0.5);

    vec3 shallow = vec3(0.11, 0.48, 0.55);
    vec3 deep = vec3(0.03, 0.20, 0.29);
    vec3 color = mix(shallow, deep, depth_t);
    color *= 0.97 + (ripple * 0.012);

    vec3 normal = normalize(frag_normal);
    vec3 light_direction = normalize(pc.light_direction_debug.xyz);
    float sparkle = pow(max(dot(normal, light_direction), 0.0), 18.0);
    color += vec3(0.06, 0.09, 0.08) * sparkle;
    color = mix(color, vec3(0.82, 0.91, 0.86), shore_t * 0.32);
    float fog = smoothstep(420.0, 1150.0, length(frag_world_position.xz));
    color = mix(color, vec3(0.61, 0.74, 0.83), fog);
    return color;
}

vec3 terrain_final_color() {
    vec3 color = material_color(frag_material);
    float water_depth = frag_fields.y;
    float water = smoothstep(0.0, 38.0, water_depth);
    vec3 submerged = mix(vec3(0.11, 0.44, 0.51), vec3(0.04, 0.22, 0.30),
                         smoothstep(4.0, 82.0, water_depth));
    color = mix(color, submerged, water * 0.90);

    float detail = (hash21(frag_world_position.xz * 0.055) - 0.5) +
                   (sin(frag_world_position.x * 0.09) * sin(frag_world_position.z * 0.07) * 0.35);
    color *= 0.96 + (detail * 0.08 * (1.0 - (water * 0.85)));

    vec3 normal = normalize(frag_normal);
    vec3 light_direction = normalize(pc.light_direction_debug.xyz);
    float diffuse = max(dot(normal, light_direction), 0.0);
    float wrap = max(dot(normal, normalize(vec3(-0.4, 0.32, -0.6))), 0.0);
    float sky = clamp(normal.y, 0.0, 1.0);
    float lighting = 0.30 + diffuse * 0.58 + wrap * 0.12 + sky * 0.10;
    float elevation_lift = smoothstep(pc.field_ranges.x, pc.field_ranges.y, frag_fields.x) * 0.06;
    vec3 lit = color * lighting + vec3(elevation_lift);
    float fog = smoothstep(820.0, 2200.0, length(frag_world_position.xz));
    return mix(lit, vec3(0.58, 0.70, 0.79), fog * 0.35);
}

void main() {
    uint debug_view = uint(pc.light_direction_debug.w + 0.5);
    float height_t = (frag_fields.x - pc.field_ranges.x) /
                     max(pc.field_ranges.y - pc.field_ranges.x, 0.001);
    float depth_t = frag_fields.y / max(pc.field_ranges.z, 0.001);
    float shore_t = clamp((frag_fields.z / max(pc.field_ranges.w, 0.001)) * 0.5 + 0.5, 0.0, 1.0);
    float slope_t = clamp(frag_fields.w * 1.8, 0.0, 1.0);

    bool water_surface = frag_fields.w < -0.5;
    vec3 color = water_surface ? water_surface_color() : terrain_final_color();
    if (debug_view == TERRAIN_VIEW_HEIGHT) {
        color = ramp3(height_t, vec3(0.05, 0.12, 0.25), vec3(0.46, 0.58, 0.35),
                      vec3(0.91, 0.86, 0.74));
    } else if (debug_view == TERRAIN_VIEW_WATER_DEPTH) {
        color = ramp3(depth_t, vec3(0.72, 0.86, 0.90), vec3(0.10, 0.34, 0.52),
                      vec3(0.02, 0.05, 0.16));
    } else if (debug_view == TERRAIN_VIEW_SHORELINE) {
        vec3 water_side = mix(vec3(0.02, 0.15, 0.28), vec3(0.55, 0.78, 0.86), shore_t * 2.0);
        vec3 land_side = mix(vec3(0.92, 0.84, 0.62), vec3(0.20, 0.43, 0.22), (shore_t - 0.5) * 2.0);
        color = shore_t < 0.5 ? water_side : land_side;
        color = mix(vec3(1.0), color, smoothstep(0.0, 0.04, abs(shore_t - 0.5)));
    } else if (debug_view == TERRAIN_VIEW_MATERIAL) {
        color = frag_material.rgb;
        color = vec3(frag_material.r, frag_material.g, max(frag_material.b, frag_material.a));
    } else if (debug_view == TERRAIN_VIEW_SLOPE) {
        color = ramp3(slope_t, vec3(0.10, 0.18, 0.22), vec3(0.78, 0.58, 0.26),
                      vec3(0.93, 0.92, 0.86));
    } else if (debug_view == TERRAIN_VIEW_LANDFORM) {
        color = ramp3(frag_generator.x, vec3(0.03, 0.12, 0.22), vec3(0.78, 0.80, 0.58),
                      vec3(0.20, 0.46, 0.22));
    } else if (debug_view == TERRAIN_VIEW_RIDGES) {
        color = ramp3(frag_generator.z, vec3(0.09, 0.13, 0.16), vec3(0.48, 0.42, 0.33),
                      vec3(0.92, 0.88, 0.78));
    } else if (debug_view == TERRAIN_VIEW_VALLEYS) {
        color = ramp3(frag_generator.w, vec3(0.12, 0.15, 0.12), vec3(0.12, 0.40, 0.36),
                      vec3(0.68, 0.88, 0.78));
    }
    out_color = vec4(color, 1.0);
}
