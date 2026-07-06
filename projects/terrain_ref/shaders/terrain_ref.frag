#version 450
#extension GL_GOOGLE_include_directive : require

#include "terrain_engine_reference.glsl"

layout(set = 0, binding = 0, std140) uniform TerrainRefFrame {
    mat4 view_projection;
    vec4 light_direction_extent;
    vec4 terrain_params;
    vec4 water_params;
    vec4 camera_position_fog;
    vec4 material_params;
} frame;

layout(set = 0, binding = 1) uniform sampler2D sand_texture;
layout(set = 0, binding = 2) uniform sampler2D grass_texture;
layout(set = 0, binding = 3) uniform sampler2D grass_variation_texture;
layout(set = 0, binding = 4) uniform sampler2D rock_texture;
layout(set = 0, binding = 5) uniform sampler2D snow_texture;
layout(set = 0, binding = 6) uniform sampler2D rock_normal_texture;

layout(location = 0) in vec3 frag_world_position;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec2 frag_material_uv;
layout(location = 3) in float frag_height_m;
layout(location = 4) in float frag_water_mask;

layout(location = 0) out vec4 out_color;

vec3 terrain_ref_rock_normal(vec3 normal, vec2 uv) {
    vec3 tangent = normalize(vec3(1.0, 0.0, 0.0) - normal * dot(normal, vec3(1.0, 0.0, 0.0)));
    if (dot(tangent, tangent) < 0.001) {
        tangent = vec3(0.0, 0.0, 1.0);
    }
    vec3 bitangent = normalize(cross(normal, tangent));
    vec3 detail = texture(rock_normal_texture, uv * vec2(0.35, 0.42)).rgb * 2.0 - 1.0;
    detail = normalize(vec3(detail.r, detail.b * 1.35, detail.g));
    return normalize(mat3(tangent, bitangent, normal) * detail);
}

vec3 terrain_ref_material_color(inout vec3 normal) {
    float grass_coverage = frame.material_params.x;
    float transition_m = frame.material_params.y;
    float water_height_m = frame.water_params.x;
    float min_height_m = frame.water_params.y;
    float max_height_m = max(frame.water_params.z, water_height_m + 1.0);
    float cos_v = clamp(normal.y, 0.0, 1.0);
    float camera_dist = distance(frame.camera_position_fog.xyz, frag_world_position);
    float texture_detail = 0.30 + 0.58 * (1.0 - smoothstep(650.0, 4600.0, camera_dist));

    vec3 sand = texture(sand_texture, frag_material_uv * 0.72).rgb * vec3(1.12, 1.04, 0.88);
    vec3 grass_a = texture(grass_texture, frag_material_uv * 0.88).rgb * vec3(0.74, 0.84, 0.62);
    vec3 grass_b = texture(grass_variation_texture, frag_material_uv * 0.52).rgb *
        vec3(0.48, 0.66, 0.38);
    float grass_mix = smoothstep(0.18, 0.82,
        terrain_engine_reference_noise(frag_world_position.xz * 0.0015, frame.terrain_params.xy));
    vec3 grass = mix(grass_a, grass_b, grass_mix);
    vec3 rock = texture(rock_texture, frag_material_uv * vec2(0.18, 0.23)).rgb *
        vec3(1.55, 1.42, 1.24);
    vec3 snow = texture(snow_texture, frag_material_uv * 0.30).rgb * 1.18;
    sand = mix(vec3(0.67, 0.58, 0.38), sand, texture_detail * 0.66);
    grass = mix(vec3(0.23, 0.36, 0.16), grass, texture_detail * 0.72);
    rock = mix(vec3(0.42, 0.40, 0.35), rock, texture_detail * 0.70);
    snow = mix(vec3(0.82, 0.84, 0.80), snow, texture_detail * 0.58);

    vec3 color = rock;
    float ten_percent_grass = grass_coverage - grass_coverage * 0.1;
    if (frag_height_m <= water_height_m + transition_m) {
        color = sand;
    } else if (frag_height_m <= water_height_m + transition_m * 2.0) {
        float blend = clamp((frag_height_m - water_height_m - transition_m) / transition_m,
            0.0, 1.0);
        color = mix(sand, grass, blend);
    } else if (cos_v > grass_coverage) {
        color = grass;
        normal = normalize(mix(normal, vec3(0.0, 1.0, 0.0), 0.16));
    } else if (cos_v > ten_percent_grass) {
        float blend = clamp((cos_v - ten_percent_grass) / max(grass_coverage * 0.1, 0.001),
            0.0, 1.0);
        color = mix(rock, grass, blend);
        normal = normalize(mix(terrain_ref_rock_normal(normal, frag_material_uv), normal, blend));
    } else {
        color = rock;
        normal = terrain_ref_rock_normal(normal, frag_material_uv);
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

    vec3 light_direction = normalize(frame.light_direction_extent.xyz);
    float diffuse = max(dot(normal, light_direction), 0.0);
    vec3 view_direction = normalize(frame.camera_position_fog.xyz - frag_world_position);
    vec3 half_vector = normalize(light_direction + view_direction);
    float specular = pow(max(dot(normal, half_vector), 0.0), mix(24.0, 120.0, water_mask));
    float sky = clamp(normal.y * 0.55 + 0.45, 0.0, 1.0);
    float rim = max(dot(normal, normalize(vec3(-0.55, 0.35, -0.38))), 0.0);
    float lighting = 0.24 + diffuse * 0.78 + sky * 0.18 + rim * 0.08;
    vec3 terrain_color = base_color * lighting + vec3(specular) * 0.035;

    vec3 water_color = mix(vec3(0.03, 0.16, 0.21), vec3(0.10, 0.48, 0.60),
        smoothstep(frame.water_params.x - 40.0, frame.water_params.x, frag_height_m));
    water_color = water_color * (0.42 + diffuse * 0.35 + sky * 0.18) + vec3(specular) * 0.35;
    vec3 color = mix(terrain_color, water_color, water_mask);

    float extent = max(frame.light_direction_extent.w, 1.0);
    float dist = distance(frame.camera_position_fog.xyz, frag_world_position);
    float distance_fog = smoothstep(extent * 0.36, extent * 1.15, dist);
    float altitude_fog = clamp(exp(-max(frame.camera_position_fog.y, 0.0) *
        frame.camera_position_fog.w), 0.15, 1.0);
    float grazing_fog = smoothstep(-0.12, 0.18, -view_direction.y);
    float fog = clamp(distance_fog * altitude_fog * (0.32 + grazing_fog * 0.24), 0.0, 0.46);
    vec3 fog_color = vec3(0.50, 0.61, 0.67);
    color = mix(color, fog_color, fog);
    color = pow(clamp(color * 1.10, 0.0, 1.0), vec3(1.0 / 2.2));
    out_color = vec4(color, 1.0);
}
