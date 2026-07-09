#version 450
#extension GL_GOOGLE_include_directive : require

#include "terrain_engine_reference.glsl"
#include "shadertoy_biome_reference.glsl"

layout(push_constant) uniform TerrainRefPushConstants {
    mat4 view_projection;
    vec4 light_direction_extent;
    vec4 terrain_params;
    vec4 water_params;
    vec4 camera_position_fog;
} pc;

layout(location = 0) in vec3 frag_world_position;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec2 frag_material_uv;
layout(location = 3) in float frag_height_m;
layout(location = 4) in float frag_water_mask;

layout(location = 0) out vec4 out_color;

float terrain_ref_fbm(vec2 p, vec2 seed) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    mat2 rotation = mat2(0.80, -0.60, 0.60, 0.80);
    for (int octave = 0; octave < 4; ++octave) {
        vec2 octave_seed = seed + vec2(float(octave) * 13.17, float(octave) * 7.31);
        value += terrain_engine_reference_noise(rotation * p * frequency, octave_seed) * amplitude;
        frequency *= 2.07;
        amplitude *= 0.5;
    }
    return value;
}

vec3 terrain_ref_detail_normal(vec3 normal, vec2 world_xz, float strength) {
    vec2 seed = pc.terrain_params.xy + vec2(41.0, 17.0);
    float sample_scale = 0.018;
    float step_m = 5.0;
    float center = terrain_ref_fbm(world_xz * sample_scale, seed);
    float dx = terrain_ref_fbm((world_xz + vec2(step_m, 0.0)) * sample_scale, seed) - center;
    float dz = terrain_ref_fbm((world_xz + vec2(0.0, step_m)) * sample_scale, seed) - center;
    vec3 detail_slope = normalize(vec3(-dx * 14.0, 1.0, -dz * 14.0));
    return normalize(mix(normal, detail_slope, strength));
}

vec3 terrain_ref_color_variation(vec3 base, vec2 world_xz, float scale, float amount) {
    float noise = terrain_ref_fbm(world_xz * scale, pc.terrain_params.xy + vec2(3.0, 23.0));
    return base * (1.0 + (noise - 0.5) * amount);
}

vec3 terrain_ref_material_color(inout vec3 normal) {
    bool shadertoy_mountain =
        abs(pc.terrain_params.w - TERRAIN_REF_RECIPE_SHADERTOY_MOUNTAIN) < 0.5;
    bool shadertoy_alpine =
        abs(pc.terrain_params.w - TERRAIN_REF_RECIPE_SHADERTOY_ALPINE) < 0.5;
    bool shadertoy_dunes =
        abs(pc.terrain_params.w - TERRAIN_REF_RECIPE_SHADERTOY_DUNES) < 0.5;
    bool shadertoy_lake_basin =
        abs(pc.terrain_params.w - TERRAIN_REF_RECIPE_SHADERTOY_LAKE_BASIN) < 0.5;
    bool height_material = pc.water_params.w > 0.5;
    float grass_coverage = 0.65;
    float transition_m = 20.0;
    float water_height_m = pc.water_params.x;
    float min_height_m = pc.water_params.y;
    float max_height_m = max(pc.water_params.z, water_height_m + 1.0);
    float cos_v = clamp(normal.y, 0.0, 1.0);
    float camera_dist = distance(pc.camera_position_fog.xyz, frag_world_position);
    float detail = 0.25 + 0.55 * (1.0 - smoothstep(700.0, 4600.0, camera_dist));

    vec3 sand = terrain_ref_color_variation(vec3(0.66, 0.58, 0.38), frag_world_position.xz,
        0.010, 0.18);
    vec3 grass = terrain_ref_color_variation(vec3(0.22, 0.34, 0.15), frag_world_position.xz,
        0.004, 0.28);
    grass = mix(grass, terrain_ref_color_variation(vec3(0.15, 0.27, 0.12),
        frag_world_position.xz + vec2(97.0, -43.0), 0.013, 0.20), 0.35);
    vec3 rock = terrain_ref_color_variation(vec3(0.43, 0.40, 0.35), frag_world_position.xz,
        0.006, 0.24);
    vec3 snow = terrain_ref_color_variation(vec3(0.82, 0.84, 0.80), frag_world_position.xz,
        0.014, 0.10);

    if (height_material) {
        float normalized_height = clamp((frag_height_m - min_height_m) /
            (max_height_m - min_height_m), 0.0, 1.0);
        vec3 low = vec3(0.25, 0.33, 0.28);
        vec3 mid = vec3(0.50, 0.49, 0.43);
        vec3 high = vec3(0.82, 0.80, 0.74);
        vec3 color = mix(low, mid, smoothstep(0.14, 0.58, normalized_height));
        color = mix(color, high, smoothstep(0.60, 0.94, normalized_height));
        float slope = 1.0 - cos_v;
        color = mix(color, vec3(0.42, 0.41, 0.39), smoothstep(0.24, 0.74, slope) * 0.42);
        normal = normalize(mix(normal, vec3(0.0, 1.0, 0.0), 0.08));
        return color;
    }

    if (shadertoy_mountain) {
        float normalized_height = clamp((frag_height_m - min_height_m) /
            (max_height_m - min_height_m), 0.0, 1.0);
        vec3 talus = terrain_ref_color_variation(vec3(0.38, 0.35, 0.30),
            frag_world_position.xz, 0.008, 0.22);
        vec3 alpine_grass = terrain_ref_color_variation(vec3(0.20, 0.30, 0.13),
            frag_world_position.xz, 0.005, 0.24);
        vec3 high_rock = terrain_ref_color_variation(vec3(0.50, 0.49, 0.45),
            frag_world_position.xz, 0.011, 0.20);
        vec3 color = mix(talus, high_rock, smoothstep(0.35, 0.82, normalized_height));
        float low_grass = smoothstep(water_height_m + transition_m,
            water_height_m + transition_m * 5.0, frag_height_m) *
            (1.0 - smoothstep(0.48, 0.76, normalized_height)) *
            smoothstep(0.56, 0.86, cos_v);
        color = mix(color, alpine_grass, low_grass * 0.82);
        float shore = 1.0 - smoothstep(water_height_m + 8.0, water_height_m + 42.0,
            frag_height_m);
        color = mix(color, sand, shore * 0.75);
        float cliff = 1.0 - smoothstep(0.42, 0.72, cos_v);
        normal = terrain_ref_detail_normal(normal, frag_world_position.xz,
            detail * mix(0.08, 0.20, cliff));
        float snow_mask = smoothstep(0.62, 0.86, normalized_height) *
            smoothstep(0.36, 0.70, cos_v);
        color = mix(color, snow, snow_mask * 0.90);
        return color;
    }

    if (shadertoy_alpine) {
        float normalized_height = clamp((frag_height_m - min_height_m) /
            (max_height_m - min_height_m), 0.0, 1.0);
        float slope = 1.0 - cos_v;
        vec3 meadow = terrain_ref_color_variation(vec3(0.18, 0.28, 0.12),
            frag_world_position.xz, 0.004, 0.26);
        vec3 talus = terrain_ref_color_variation(vec3(0.40, 0.38, 0.34),
            frag_world_position.xz, 0.009, 0.22);
        vec3 cliff_rock = terrain_ref_color_variation(vec3(0.48, 0.48, 0.45),
            frag_world_position.xz, 0.014, 0.20);
        vec3 color = mix(talus, cliff_rock, smoothstep(0.34, 0.92, normalized_height));
        float meadow_mask = (1.0 - smoothstep(0.34, 0.66, normalized_height)) *
            smoothstep(0.52, 0.88, cos_v);
        color = mix(color, meadow, meadow_mask * 0.78);
        float snow_mask = smoothstep(0.58, 0.88, normalized_height) *
            smoothstep(0.24, 0.70, cos_v);
        color = mix(color, snow, snow_mask * 0.92);
        normal = terrain_ref_detail_normal(normal, frag_world_position.xz,
            detail * mix(0.13, 0.25, smoothstep(0.18, 0.72, slope)));
        return color;
    }

    if (shadertoy_dunes) {
        vec3 low_sand = terrain_ref_color_variation(vec3(0.63, 0.52, 0.31),
            frag_world_position.xz, 0.005, 0.18);
        vec3 sun_sand = terrain_ref_color_variation(vec3(0.82, 0.69, 0.42),
            frag_world_position.xz, 0.014, 0.12);
        float slope = 1.0 - cos_v;
        vec3 color = mix(low_sand, sun_sand, smoothstep(0.42, 0.86, cos_v));
        color = mix(color, vec3(0.46, 0.36, 0.22), smoothstep(0.18, 0.58, slope) * 0.45);
        color = mix(color, vec3(0.91, 0.80, 0.52), smoothstep(0.82, 0.98, cos_v) * 0.22);
        normal = terrain_ref_detail_normal(normal, frag_world_position.xz, detail * 0.12);
        return color;
    }

    if (shadertoy_lake_basin) {
        float normalized_height = clamp((frag_height_m - min_height_m) /
            (max_height_m - min_height_m), 0.0, 1.0);
        vec3 wet_soil = terrain_ref_color_variation(vec3(0.23, 0.26, 0.16),
            frag_world_position.xz, 0.008, 0.24);
        vec3 upland_grass = terrain_ref_color_variation(vec3(0.21, 0.33, 0.15),
            frag_world_position.xz, 0.004, 0.26);
        vec3 hill_rock = terrain_ref_color_variation(vec3(0.42, 0.39, 0.33),
            frag_world_position.xz, 0.009, 0.20);
        vec3 color = mix(upland_grass, hill_rock,
            smoothstep(0.45, 0.86, normalized_height) * (1.0 - smoothstep(0.70, 0.92, cos_v)));
        float shore = 1.0 - smoothstep(water_height_m + 8.0, water_height_m + 92.0,
            frag_height_m);
        vec3 dry_shelf = terrain_ref_color_variation(vec3(0.36, 0.35, 0.23),
            frag_world_position.xz + vec2(-61.0, 71.0), 0.010, 0.18);
        color = mix(color, dry_shelf, shore * 0.34);
        color = mix(color, wet_soil, shore * 0.78);
        normal = terrain_ref_detail_normal(normal, frag_world_position.xz,
            detail * mix(0.06, 0.15, 1.0 - cos_v));
        return color;
    }

    vec3 color = rock;
    float ten_percent_grass = grass_coverage - grass_coverage * 0.1;
    if (frag_height_m <= water_height_m + transition_m) {
        color = sand;
        normal = terrain_ref_detail_normal(normal, frag_world_position.xz, detail * 0.05);
    } else if (frag_height_m <= water_height_m + transition_m * 2.0) {
        float blend = clamp((frag_height_m - water_height_m - transition_m) / transition_m,
            0.0, 1.0);
        color = mix(sand, grass, blend);
        normal = terrain_ref_detail_normal(normal, frag_world_position.xz, detail * 0.05);
    } else if (cos_v > grass_coverage) {
        color = grass;
        normal = normalize(mix(terrain_ref_detail_normal(normal, frag_world_position.xz,
            detail * 0.06), vec3(0.0, 1.0, 0.0), 0.12));
    } else if (cos_v > ten_percent_grass) {
        float blend = clamp((cos_v - ten_percent_grass) / max(grass_coverage * 0.1, 0.001),
            0.0, 1.0);
        color = mix(rock, grass, blend);
        normal = terrain_ref_detail_normal(normal, frag_world_position.xz,
            detail * mix(0.14, 0.06, blend));
    } else {
        color = rock;
        normal = terrain_ref_detail_normal(normal, frag_world_position.xz, detail * 0.17);
    }

    float normalized_height = clamp((frag_height_m - min_height_m) / (max_height_m - min_height_m),
        0.0, 1.0);
    float snow_mask = smoothstep(0.72, 0.90, normalized_height) * smoothstep(0.28, 0.62, cos_v);
    color = mix(color, snow, snow_mask * 0.82);

    return color;
}

void main() {
    vec3 normal = normalize(frag_normal);
    vec3 base_color = terrain_ref_material_color(normal);
    float water_mask = clamp(frag_water_mask, 0.0, 1.0);

    vec3 light_direction = normalize(pc.light_direction_extent.xyz);
    float diffuse = max(dot(normal, light_direction), 0.0);
    vec3 view_direction = normalize(pc.camera_position_fog.xyz - frag_world_position);
    vec3 half_vector = normalize(light_direction + view_direction);
    float specular = pow(max(dot(normal, half_vector), 0.0), mix(24.0, 120.0, water_mask));
    float sky = clamp(normal.y * 0.55 + 0.45, 0.0, 1.0);
    float rim = max(dot(normal, normalize(vec3(-0.55, 0.35, -0.38))), 0.0);
    float lighting = 0.24 + diffuse * 0.78 + sky * 0.18 + rim * 0.08;
    vec3 terrain_color = base_color * lighting + vec3(specular) * 0.035;

    vec3 water_color = mix(vec3(0.03, 0.16, 0.21), vec3(0.10, 0.48, 0.60),
        smoothstep(pc.water_params.x - 40.0, pc.water_params.x, frag_height_m));
    water_color = water_color * (0.42 + diffuse * 0.35 + sky * 0.18) + vec3(specular) * 0.35;
    vec3 color = mix(terrain_color, water_color, water_mask);

    float extent = max(pc.light_direction_extent.w, 1.0);
    float dist = distance(pc.camera_position_fog.xyz, frag_world_position);
    float distance_fog = smoothstep(extent * 0.36, extent * 1.15, dist);
    float altitude_fog = clamp(exp(-max(pc.camera_position_fog.y, 0.0) *
        pc.camera_position_fog.w), 0.15, 1.0);
    float grazing_fog = smoothstep(-0.12, 0.18, -view_direction.y);
    float fog = clamp(distance_fog * altitude_fog * (0.32 + grazing_fog * 0.24), 0.0, 0.46);
    vec3 fog_color = vec3(0.50, 0.61, 0.67);
    color = mix(color, fog_color, fog);
    color = pow(clamp(color * 1.10, 0.0, 1.0), vec3(1.0 / 2.2));
    out_color = vec4(color, 1.0);
}
