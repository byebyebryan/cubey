#ifndef CUBEY_TERRAIN_QUALITY_MATERIAL_GLSL
#define CUBEY_TERRAIN_QUALITY_MATERIAL_GLSL

const float terrain_quality_tile_period_m = 256.0;

vec4 terrain_quality_material_texture(vec2 uv, vec4 weights) {
    return texture(terrain_ground_tile, uv) * weights.x +
        texture(terrain_scree_tile, uv) * weights.y +
        texture(terrain_rock_tile, uv) * weights.z +
        texture(terrain_snow_tile, uv) * weights.w;
}

vec3 terrain_quality_projection_weights(vec3 normal) {
    vec3 weights = pow(abs(normal), vec3(5.0));
    return weights / max(dot(weights, vec3(1.0)), 0.0001);
}

TerrainMaterialSample terrain_quality_material_sample(
        TerrainMaterialSample material, vec3 source_normal, vec3 world_position,
        float pixel_footprint_m) {
    vec3 warp = vec3(
        sin(world_position.z / 83.0),
        sin((world_position.x + world_position.z) / 71.0),
        sin(world_position.x / 79.0)) * 7.5;
    vec3 warped = world_position + warp;
    vec3 projection_weights = terrain_quality_projection_weights(source_normal);
    vec4 sample_x = terrain_quality_material_texture(
        warped.zy / terrain_quality_tile_period_m, material.material_weights);
    vec4 sample_y = terrain_quality_material_texture(
        warped.xz / terrain_quality_tile_period_m, material.material_weights);
    vec4 sample_z = terrain_quality_material_texture(
        warped.xy / terrain_quality_tile_period_m, material.material_weights);
    vec4 tile = sample_x * projection_weights.x + sample_y * projection_weights.y +
        sample_z * projection_weights.z;

    material.base_color *= mix(0.78, 1.22, tile.b);
    material.roughness = mix(material.roughness, tile.a, 0.72);
    float normal_visibility = 1.0 - smoothstep(0.22, 1.1, pixel_footprint_m);
    vec2 gradient = (tile.rg * 2.0 - 1.0) * normal_visibility;
    vec3 detail_normal = normalize(vec3(source_normal.x - gradient.x * 0.22,
        max(source_normal.y, 0.08), source_normal.z - gradient.y * 0.22));
    material.detail_normal = normalize(mix(material.detail_normal, detail_normal, 0.58));
    return material;
}

#endif
