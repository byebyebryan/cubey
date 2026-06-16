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
layout(location = 10) in vec4 frag_drivers;

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
const uint TERRAIN_LAB_VIEW_DRIVER = 20u;

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
    vec3 rock = vec3(0.37, 0.35, 0.31);
    vec3 soil = vec3(0.39, 0.30, 0.19);
    vec3 scree = vec3(0.52, 0.49, 0.42);
    vec3 meadow = vec3(0.30, 0.39, 0.18);
    vec3 forest = vec3(0.11, 0.22, 0.12);
    vec3 snow = vec3(0.84, 0.86, 0.82);
    vec3 sand = vec3(0.67, 0.56, 0.34);
    return rock * frag_material_a.x + soil * frag_material_a.y +
           scree * frag_material_a.z + meadow * frag_material_a.w +
           forest * frag_material_b.x + snow * frag_material_b.y +
           sand * frag_material_b.w;
}

float strata_band_strength() {
    float strata_noise = (value_noise2(frag_world_position.xz * 0.0016) - 0.5) * 0.30;
    float major_layer = fract(frag_world_position.y * 0.018 + strata_noise);
    float minor_layer = fract(frag_world_position.y * 0.057 + strata_noise * 1.7 + 0.23);
    float major = 1.0 - smoothstep(0.018, 0.078, abs(major_layer - 0.5));
    float minor = 1.0 - smoothstep(0.014, 0.046, abs(minor_layer - 0.5));
    return clamp(max(major * 0.85, minor * 0.42), 0.0, 1.0);
}

float caprock_strength() {
    float rim = clamp(frag_influences.x * 0.55 + frag_influences.z * 0.45, 0.0, 1.0);
    float upper = smoothstep(0.62, 0.92, frag_terrain.w);
    float ledge = smoothstep(0.22, 0.68, normalize(frag_normal).y);
    return rim * upper * ledge * (1.0 - clamp(frag_influences.w, 0.0, 1.0) * 0.55);
}

float sparse_spot(vec2 world_position, float cell_size_m, float density, float radius, float salt) {
    vec2 grid_position = world_position / cell_size_m;
    vec2 cell = floor(grid_position);
    float selector = hash21(cell + vec2(salt, salt * 1.37));
    if (selector > density) {
        return 0.0;
    }

    vec2 center = vec2(hash21(cell + vec2(7.1 + salt, 3.7)),
                       hash21(cell + vec2(2.4, 9.2 + salt)));
    center = mix(vec2(0.18), vec2(0.82), center);
    float distance_to_center = length(fract(grid_position) - center);
    return 1.0 - smoothstep(radius, radius + 0.045, distance_to_center);
}

vec3 final_color() {
    vec3 color = material_color();
    float grain = (value_noise2(frag_world_position.xz * 0.012) - 0.5) * 0.05;
    grain += (value_noise2(frag_world_position.xz * 0.041 + vec2(19.0, -7.0)) - 0.5) * 0.035;
    color *= 1.0 + grain;
    float proxy_geometry = frag_feature_tags.w < -0.5 ? 1.0 : 0.0;
    color = mix(color, vec3(0.16, 0.24, 0.09),
                proxy_geometry * clamp(frag_vegetation.y, 0.0, 1.0) * 0.78);
    color = mix(color, vec3(0.22, 0.20, 0.16),
                proxy_geometry * clamp(frag_material_a.x + frag_material_a.z, 0.0, 1.0) * 0.55);
    float channel = clamp(frag_influences.w, 0.0, 1.0);
    float exposed = clamp((frag_material_a.x + frag_material_a.z) * 0.44 +
                              frag_influences.x * 0.46 + frag_influences.z * 0.22,
                          0.0, 1.0) *
                    (1.0 - channel * 0.52);
    float wall = smoothstep(0.08, 0.58, 1.0 - normalize(frag_normal).y);
    float strata = strata_band_strength() * exposed * (0.38 + wall * 0.62);
    color = mix(color, color * vec3(0.70, 0.66, 0.58), strata * 0.58);
    color = mix(color, vec3(0.55, 0.47, 0.33), strata * 0.20);

    float caprock = caprock_strength();
    color = mix(color, vec3(0.25, 0.23, 0.19), caprock * 0.28);
    color = mix(color, vec3(0.51, 0.44, 0.31), caprock * 0.10);

    color = mix(color, vec3(0.16, 0.27, 0.21), clamp(frag_hydrology.w, 0.0, 1.0) * 0.10);
    color = mix(color, vec3(0.46, 0.38, 0.23), channel * 0.12);
    color = mix(color, vec3(0.44, 0.38, 0.25), clamp(frag_material_b.z, 0.0, 1.0) * 0.11);

    float talus_density = clamp(frag_material_a.z * 0.72 + frag_material_a.x * 0.16, 0.0, 0.56);
    float talus_proxy =
        sparse_spot(frag_world_position.xz, 34.0, talus_density, 0.095, 43.0) *
        smoothstep(0.12, 0.42, frag_material_a.z) * (0.42 + exposed * 0.58);
    color = mix(color, vec3(0.19, 0.18, 0.15), talus_proxy * 0.44);

    float scrub_density = clamp(frag_vegetation.y * 1.55, 0.0, 0.48);
    float scrub_proxy =
        sparse_spot(frag_world_position.xz, 58.0, scrub_density, 0.12, 17.0) *
        smoothstep(0.012, 0.08, frag_vegetation.y) * (1.0 - channel * 0.36);
    color = mix(color, vec3(0.18, 0.22, 0.12), scrub_proxy * 0.70);

    float grass_mottle =
        smoothstep(0.10, 0.46, frag_vegetation.x) *
        smoothstep(0.54, 0.92, value_noise2(frag_world_position.xz * 0.030 + vec2(5.0, -3.0)));
    color = mix(color, vec3(0.24, 0.31, 0.14), grass_mottle * 0.12);

    vec3 normal = normalize(frag_normal);
    vec3 light_direction = normalize(pc.light_direction_debug.xyz);
    float diffuse = max(dot(normal, light_direction), 0.0);
    float sky = clamp(normal.y, 0.0, 1.0);
    float wrap = max(dot(normal, normalize(vec3(-0.4, 0.35, -0.5))), 0.0);
    float lighting = 0.25 + diffuse * 0.66 + wrap * 0.12 + sky * 0.10;
    vec3 lit = color * lighting;

    float distance_t = smoothstep(3600.0, 9200.0, length(frag_world_position.xz));
    return mix(lit, vec3(0.56, 0.66, 0.71), distance_t * 0.22);
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
    float driver_selection_t = clamp(frag_drivers.w, 0.0, 1.0);
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
    } else if (debug_view == TERRAIN_LAB_VIEW_DRIVER) {
        color = clamp(frag_drivers.xyz, vec3(0.0), vec3(1.0)) *
                (0.35 + 0.65 * driver_selection_t);
    }

    out_color = vec4(color, 1.0);
}
