#version 450

layout(push_constant) uniform TerrainLabPushConstants {
    mat4 view_projection;
    vec4 light_direction_debug;
    vec4 field_ranges;
    vec4 contribution_ranges;
    vec4 hydrology_ranges;
} pc;

layout(location = 0) in vec3 frag_world_position;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec4 frag_terrain;
layout(location = 3) in vec4 frag_contributions;
layout(location = 4) in vec4 frag_hydrology;
layout(location = 5) in vec4 frag_material_a;
layout(location = 6) in vec4 frag_material_b;
layout(location = 7) in vec4 frag_vegetation;
layout(location = 8) in vec4 frag_influences;
layout(location = 9) in vec4 frag_feature_tags;

layout(location = 0) out vec4 out_color;

const uint TERRAIN_LAB_VIEW_FINAL = 0u;
const uint TERRAIN_LAB_VIEW_HEIGHT = 1u;
const uint TERRAIN_LAB_VIEW_STRUCTURE = 2u;
const uint TERRAIN_LAB_VIEW_PROCESS = 3u;
const uint TERRAIN_LAB_VIEW_DETAIL = 4u;
const uint TERRAIN_LAB_VIEW_SLOPE = 5u;
const uint TERRAIN_LAB_VIEW_CURVATURE = 6u;
const uint TERRAIN_LAB_VIEW_FLOW_DIRECTION = 7u;
const uint TERRAIN_LAB_VIEW_FLOW_ACCUMULATION = 8u;
const uint TERRAIN_LAB_VIEW_STREAM_POWER = 9u;
const uint TERRAIN_LAB_VIEW_WETNESS = 10u;
const uint TERRAIN_LAB_VIEW_DEPOSITION = 11u;
const uint TERRAIN_LAB_VIEW_MATERIAL = 12u;
const uint TERRAIN_LAB_VIEW_BIOME_DENSITY = 13u;
const uint TERRAIN_LAB_VIEW_CANOPY_HEIGHT = 14u;
const uint TERRAIN_LAB_VIEW_NOISE_OFF = 15u;
const uint TERRAIN_LAB_VIEW_FEATURE_GRAPH = 16u;
const uint TERRAIN_LAB_VIEW_WATERSHED = 17u;
const uint TERRAIN_LAB_VIEW_CHANNEL = 18u;
const uint TERRAIN_LAB_VIEW_DIVIDE = 19u;

vec3 ramp3(float t, vec3 a, vec3 b, vec3 c) {
    t = clamp(t, 0.0, 1.0);
    return t < 0.5 ? mix(a, b, t * 2.0) : mix(b, c, (t - 0.5) * 2.0);
}

vec3 signed_ramp(float value, float max_abs_value) {
    float t = clamp(value / max(max_abs_value, 0.001), -1.0, 1.0);
    vec3 zero = vec3(0.12, 0.14, 0.15);
    vec3 negative = vec3(0.14, 0.34, 0.62);
    vec3 positive = vec3(0.94, 0.70, 0.34);
    return t < 0.0 ? mix(zero, negative, -t) : mix(zero, positive, t);
}

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float value_noise2(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

vec3 material_color() {
    vec3 rock = vec3(0.35, 0.36, 0.34);
    vec3 soil = vec3(0.34, 0.28, 0.20);
    vec3 scree = vec3(0.49, 0.48, 0.44);
    vec3 meadow = vec3(0.27, 0.43, 0.21);
    vec3 forest = vec3(0.10, 0.24, 0.13);
    vec3 snow = vec3(0.84, 0.86, 0.82);
    return rock * frag_material_a.x + soil * frag_material_a.y +
           scree * frag_material_a.z + meadow * frag_material_a.w +
           forest * frag_material_b.x + snow * frag_material_b.y;
}

vec3 final_color() {
    vec3 color = material_color();
    float grain = (value_noise2(frag_world_position.xz * 0.012) - 0.5) * 0.05;
    grain += (value_noise2(frag_world_position.xz * 0.041 + vec2(19.0, -7.0)) - 0.5) * 0.035;
    color *= 1.0 + grain;
    color = mix(color, vec3(0.16, 0.27, 0.21), clamp(frag_hydrology.w, 0.0, 1.0) * 0.14);
    color = mix(color, vec3(0.18, 0.23, 0.20), clamp(frag_influences.w, 0.0, 1.0) * 0.045);
    color = mix(color, vec3(0.42, 0.37, 0.25), clamp(frag_material_b.z, 0.0, 1.0) * 0.08);

    vec3 normal = normalize(frag_normal);
    vec3 light_direction = normalize(pc.light_direction_debug.xyz);
    float diffuse = max(dot(normal, light_direction), 0.0);
    float sky = clamp(normal.y, 0.0, 1.0);
    float wrap = max(dot(normal, normalize(vec3(-0.4, 0.35, -0.5))), 0.0);
    float lighting = 0.28 + diffuse * 0.58 + wrap * 0.10 + sky * 0.12;
    vec3 lit = color * lighting;

    float distance_t = smoothstep(3600.0, 9200.0, length(frag_world_position.xz));
    return mix(lit, vec3(0.58, 0.68, 0.73), distance_t * 0.25);
}

vec3 flow_direction_color(float direction) {
    float t = clamp(direction / 8.0, 0.0, 1.0);
    if (direction > 7.5) {
        return vec3(0.12, 0.12, 0.13);
    }
    return vec3(0.5 + 0.5 * cos(6.28318 * (t + 0.00)),
                0.5 + 0.5 * cos(6.28318 * (t + 0.33)),
                0.5 + 0.5 * cos(6.28318 * (t + 0.66)));
}

vec3 watershed_color(float id_t) {
    float slot = floor(clamp(id_t, 0.0, 1.0) * 3.0 + 0.5);
    if (slot < 0.5) {
        return vec3(0.35, 0.48, 0.78);
    }
    if (slot < 1.5) {
        return vec3(0.32, 0.62, 0.43);
    }
    if (slot < 2.5) {
        return vec3(0.78, 0.55, 0.28);
    }
    return vec3(0.62, 0.42, 0.72);
}

void main() {
    uint debug_view = uint(pc.light_direction_debug.w + 0.5);
    float height_t = (frag_terrain.x - pc.field_ranges.x) /
                     max(pc.field_ranges.y - pc.field_ranges.x, 0.001);
    float slope_t = clamp(frag_terrain.y / max(pc.field_ranges.z, 0.001), 0.0, 1.0);
    float curvature_t = clamp(abs(frag_terrain.z) / max(pc.field_ranges.w, 0.001), 0.0, 1.0);
    float flow_t = log(1.0 + max(frag_hydrology.y, 0.0)) /
                   max(log(1.0 + pc.hydrology_ranges.x), 0.001);
    float stream_t = clamp(frag_hydrology.z / max(pc.hydrology_ranges.y, 0.001), 0.0, 1.0);
    float canopy_t = clamp(frag_vegetation.w / max(pc.hydrology_ranges.z, 0.001), 0.0, 1.0);
    float divide_t = clamp(frag_influences.z, 0.0, 1.0);
    float channel_t = clamp(frag_influences.w, 0.0, 1.0);
    float channel_distance_t = clamp(frag_feature_tags.y, 0.0, 1.0);
    vec3 basin_color = watershed_color(frag_feature_tags.x);

    vec3 color = final_color();
    if (debug_view == TERRAIN_LAB_VIEW_HEIGHT) {
        color = ramp3(height_t, vec3(0.07, 0.16, 0.28), vec3(0.42, 0.54, 0.32),
                      vec3(0.88, 0.82, 0.67));
    } else if (debug_view == TERRAIN_LAB_VIEW_STRUCTURE) {
        color = signed_ramp(frag_contributions.x, pc.contribution_ranges.x);
    } else if (debug_view == TERRAIN_LAB_VIEW_PROCESS) {
        color = signed_ramp(frag_contributions.y, pc.contribution_ranges.y);
    } else if (debug_view == TERRAIN_LAB_VIEW_DETAIL) {
        color = signed_ramp(frag_contributions.z, pc.contribution_ranges.z);
    } else if (debug_view == TERRAIN_LAB_VIEW_SLOPE) {
        color = ramp3(slope_t, vec3(0.09, 0.17, 0.20), vec3(0.78, 0.58, 0.26),
                      vec3(0.92, 0.90, 0.82));
    } else if (debug_view == TERRAIN_LAB_VIEW_CURVATURE) {
        color = signed_ramp(frag_terrain.z, pc.field_ranges.w);
    } else if (debug_view == TERRAIN_LAB_VIEW_FLOW_DIRECTION) {
        color = flow_direction_color(frag_hydrology.x);
    } else if (debug_view == TERRAIN_LAB_VIEW_FLOW_ACCUMULATION) {
        color = ramp3(flow_t, vec3(0.09, 0.11, 0.16), vec3(0.10, 0.43, 0.52),
                      vec3(0.75, 0.92, 0.82));
    } else if (debug_view == TERRAIN_LAB_VIEW_STREAM_POWER) {
        color = ramp3(stream_t, vec3(0.12, 0.10, 0.08), vec3(0.46, 0.29, 0.14),
                      vec3(0.94, 0.76, 0.32));
    } else if (debug_view == TERRAIN_LAB_VIEW_WETNESS) {
        color = ramp3(frag_hydrology.w, vec3(0.52, 0.42, 0.24), vec3(0.19, 0.48, 0.42),
                      vec3(0.68, 0.88, 0.86));
    } else if (debug_view == TERRAIN_LAB_VIEW_DEPOSITION) {
        color = ramp3(frag_material_b.z, vec3(0.14, 0.13, 0.12), vec3(0.48, 0.37, 0.22),
                      vec3(0.83, 0.72, 0.48));
    } else if (debug_view == TERRAIN_LAB_VIEW_MATERIAL) {
        color = material_color();
    } else if (debug_view == TERRAIN_LAB_VIEW_BIOME_DENSITY) {
        color = vec3(frag_vegetation.x, frag_vegetation.y, frag_vegetation.z);
    } else if (debug_view == TERRAIN_LAB_VIEW_CANOPY_HEIGHT) {
        color = ramp3(canopy_t, vec3(0.10, 0.13, 0.11), vec3(0.22, 0.42, 0.18),
                      vec3(0.58, 0.72, 0.40));
    } else if (debug_view == TERRAIN_LAB_VIEW_NOISE_OFF) {
        color = signed_ramp(frag_contributions.w, pc.contribution_ranges.w);
    } else if (debug_view == TERRAIN_LAB_VIEW_FEATURE_GRAPH) {
        color = basin_color * 0.58;
        color = mix(color, vec3(0.92, 0.80, 0.38), divide_t * 0.72);
        color = mix(color, vec3(0.10, 0.50, 0.78), channel_t * 0.86);
    } else if (debug_view == TERRAIN_LAB_VIEW_WATERSHED) {
        color = basin_color;
        color = mix(color, vec3(0.08, 0.10, 0.12), divide_t * 0.38);
    } else if (debug_view == TERRAIN_LAB_VIEW_CHANNEL) {
        color = ramp3(channel_t, vec3(0.12, 0.11, 0.09), vec3(0.11, 0.38, 0.50),
                      vec3(0.72, 0.93, 0.96));
        color = mix(color, vec3(0.08, 0.09, 0.10), channel_distance_t * 0.25);
    } else if (debug_view == TERRAIN_LAB_VIEW_DIVIDE) {
        color = ramp3(divide_t, vec3(0.10, 0.12, 0.13), vec3(0.58, 0.42, 0.21),
                      vec3(0.95, 0.82, 0.45));
    }

    out_color = vec4(color, 1.0);
}
