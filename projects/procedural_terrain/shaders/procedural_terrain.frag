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

layout(location = 0) out vec4 out_color;

const uint TERRAIN_VIEW_FINAL = 0u;
const uint TERRAIN_VIEW_HEIGHT = 1u;
const uint TERRAIN_VIEW_WATER_DEPTH = 2u;
const uint TERRAIN_VIEW_SHORELINE = 3u;
const uint TERRAIN_VIEW_MATERIAL = 4u;
const uint TERRAIN_VIEW_SLOPE = 5u;

vec3 ramp3(float t, vec3 a, vec3 b, vec3 c) {
    t = clamp(t, 0.0, 1.0);
    return t < 0.5 ? mix(a, b, t * 2.0) : mix(b, c, (t - 0.5) * 2.0);
}

vec3 material_color(vec4 material) {
    vec3 sand = vec3(0.78, 0.62, 0.38);
    vec3 rock = vec3(0.37, 0.38, 0.36);
    vec3 vegetation = vec3(0.22, 0.47, 0.25);
    vec3 sediment = vec3(0.34, 0.31, 0.24);
    return sand * material.r + rock * material.g + vegetation * material.b + sediment * material.a;
}

vec3 final_color() {
    vec3 color = material_color(frag_material);
    float water_depth = frag_fields.y;
    float water = smoothstep(0.0, 38.0, water_depth);
    color = mix(color, vec3(0.05, 0.12, 0.16), water * 0.58);

    vec3 normal = normalize(frag_normal);
    vec3 light_direction = normalize(pc.light_direction_debug.xyz);
    float diffuse = max(dot(normal, light_direction), 0.0);
    float wrap = max(dot(normal, normalize(vec3(-0.4, 0.35, -0.6))), 0.0);
    float lighting = 0.33 + diffuse * 0.56 + wrap * 0.10;
    float elevation_lift = smoothstep(pc.field_ranges.x, pc.field_ranges.y, frag_fields.x) * 0.10;
    return color * lighting + vec3(elevation_lift);
}

void main() {
    uint debug_view = uint(pc.light_direction_debug.w + 0.5);
    float height_t = (frag_fields.x - pc.field_ranges.x) /
                     max(pc.field_ranges.y - pc.field_ranges.x, 0.001);
    float depth_t = frag_fields.y / max(pc.field_ranges.z, 0.001);
    float shore_t = clamp((frag_fields.z / max(pc.field_ranges.w, 0.001)) * 0.5 + 0.5, 0.0, 1.0);
    float slope_t = clamp(frag_fields.w * 1.8, 0.0, 1.0);

    vec3 color = final_color();
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
    }
    out_color = vec4(color, 1.0);
}
