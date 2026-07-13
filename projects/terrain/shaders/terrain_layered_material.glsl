#ifndef CUBEY_TERRAIN_LAYERED_MATERIAL_GLSL
#define CUBEY_TERRAIN_LAYERED_MATERIAL_GLSL

const float terrain_layered_tile_period_m = 256.0;

struct TerrainLayerTextureSample {
    vec3 albedo;
    float height;
    vec3 normal;
    float roughness;
    float cavity;
};

vec4 terrain_layered_albedo_height(int material, vec2 uv, vec2 gradient_x, vec2 gradient_y) {
    if (material == 0) {
        return textureGrad(terrain_ground_albedo_height, uv, gradient_x, gradient_y);
    }
    if (material == 1) {
        return textureGrad(terrain_scree_albedo_height, uv, gradient_x, gradient_y);
    }
    if (material == 2) {
        return textureGrad(terrain_rock_albedo_height, uv, gradient_x, gradient_y);
    }
    return textureGrad(terrain_snow_albedo_height, uv, gradient_x, gradient_y);
}

vec4 terrain_layered_normal_roughness(int material, vec2 uv, vec2 gradient_x, vec2 gradient_y) {
    if (material == 0) {
        return textureGrad(terrain_ground_normal_roughness, uv, gradient_x, gradient_y);
    }
    if (material == 1) {
        return textureGrad(terrain_scree_normal_roughness, uv, gradient_x, gradient_y);
    }
    if (material == 2) {
        return textureGrad(terrain_rock_normal_roughness, uv, gradient_x, gradient_y);
    }
    return textureGrad(terrain_snow_normal_roughness, uv, gradient_x, gradient_y);
}

vec3 terrain_layered_tangent_normal(vec2 encoded_xy) {
    vec2 xy = encoded_xy * 2.0 - 1.0;
    return normalize(vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.001))));
}

vec3 terrain_layered_world_normal(vec3 tangent_normal, int axis, vec3 source_normal) {
    if (axis == 0) {
        float sign_x = source_normal.x < 0.0 ? -1.0 : 1.0;
        return normalize(vec3(tangent_normal.z * sign_x, tangent_normal.y,
                              tangent_normal.x * sign_x));
    }
    if (axis == 1) {
        return normalize(vec3(tangent_normal.x, tangent_normal.z, tangent_normal.y));
    }
    float sign_z = source_normal.z < 0.0 ? -1.0 : 1.0;
    return normalize(vec3(tangent_normal.x * sign_z, tangent_normal.y,
                          tangent_normal.z * sign_z));
}

TerrainLayerTextureSample terrain_layered_sample(int material, vec3 source_normal,
                                                 vec3 warped_position) {
    vec3 projection_weights = pow(abs(source_normal), vec3(5.0));
    projection_weights /= max(dot(projection_weights, vec3(1.0)), 0.0001);
    vec3 position_dx = dFdx(warped_position) / terrain_layered_tile_period_m;
    vec3 position_dy = dFdy(warped_position) / terrain_layered_tile_period_m;
    vec2 uv_x = warped_position.zy / terrain_layered_tile_period_m;
    vec2 uv_y = warped_position.xz / terrain_layered_tile_period_m;
    vec2 uv_z = warped_position.xy / terrain_layered_tile_period_m;

    vec4 albedo_height_x = terrain_layered_albedo_height(
        material, uv_x, position_dx.zy, position_dy.zy);
    vec4 albedo_height_y = terrain_layered_albedo_height(
        material, uv_y, position_dx.xz, position_dy.xz);
    vec4 albedo_height_z = terrain_layered_albedo_height(
        material, uv_z, position_dx.xy, position_dy.xy);
    vec4 normal_roughness_x = terrain_layered_normal_roughness(
        material, uv_x, position_dx.zy, position_dy.zy);
    vec4 normal_roughness_y = terrain_layered_normal_roughness(
        material, uv_y, position_dx.xz, position_dy.xz);
    vec4 normal_roughness_z = terrain_layered_normal_roughness(
        material, uv_z, position_dx.xy, position_dy.xy);

    TerrainLayerTextureSample result;
    result.albedo = albedo_height_x.rgb * projection_weights.x +
        albedo_height_y.rgb * projection_weights.y +
        albedo_height_z.rgb * projection_weights.z;
    result.height = dot(vec3(albedo_height_x.a, albedo_height_y.a, albedo_height_z.a),
                        projection_weights);
    result.normal = normalize(
        terrain_layered_world_normal(terrain_layered_tangent_normal(normal_roughness_x.xy), 0,
                                     source_normal) * projection_weights.x +
        terrain_layered_world_normal(terrain_layered_tangent_normal(normal_roughness_y.xy), 1,
                                     source_normal) * projection_weights.y +
        terrain_layered_world_normal(terrain_layered_tangent_normal(normal_roughness_z.xy), 2,
                                     source_normal) * projection_weights.z);
    result.roughness = dot(vec3(normal_roughness_x.z, normal_roughness_y.z,
                                normal_roughness_z.z), projection_weights);
    result.cavity = dot(vec3(normal_roughness_x.w, normal_roughness_y.w,
                            normal_roughness_z.w), projection_weights);
    return result;
}

TerrainMaterialSample terrain_layered_material_sample(TerrainMaterialSample material,
                                                      vec3 source_normal,
                                                      vec3 world_position,
                                                      float pixel_footprint_m) {
    vec3 warp = vec3(
        sin(world_position.z / 173.0) +
            0.55 * sin((world_position.x - world_position.z) / 97.0),
        sin((world_position.x + world_position.z) / 149.0) +
            0.50 * sin((world_position.x - world_position.z) / 61.0),
        sin(world_position.x / 181.0) +
            0.55 * sin((world_position.x + world_position.z) / 89.0)) * 18.0;
    vec3 warped_position = world_position + warp;
    TerrainLayerTextureSample layers[4];
    for (int index = 0; index < 4; ++index) {
        layers[index] = terrain_layered_sample(index, source_normal, warped_position);
    }

    vec4 heights = vec4(layers[0].height, layers[1].height,
                        layers[2].height, layers[3].height);
    vec4 adjusted_weights = material.material_weights *
        (vec4(1.0) + (heights - 0.5) * 0.24);
    adjusted_weights = max(adjusted_weights, vec4(0.0));
    adjusted_weights /= max(dot(adjusted_weights, vec4(1.0)), 0.0001);

    vec3 layered_albedo = vec3(0.0);
    vec3 layered_normal = vec3(0.0);
    float layered_roughness = 0.0;
    float layered_cavity = 0.0;
    for (int index = 0; index < 4; ++index) {
        float weight = adjusted_weights[index];
        layered_albedo += layers[index].albedo * weight;
        layered_normal += layers[index].normal * weight;
        layered_roughness += layers[index].roughness * weight;
        layered_cavity += layers[index].cavity * weight;
    }

    float normal_visibility = 1.0 - smoothstep(4.0, 12.0, pixel_footprint_m);
    float normal_strength = dot(adjusted_weights, vec4(0.14, 0.36, 0.44, 0.12));
    material.base_color = mix(material.base_color, layered_albedo, 0.78);
    material.roughness = mix(material.roughness, layered_roughness, 0.82);
    material.detail_normal = normalize(mix(source_normal, normalize(layered_normal),
        normal_strength * normal_visibility));
    material.material_weights = adjusted_weights;
    material.cavity = clamp(layered_cavity, 0.78, 1.0);
    material.blend_height = dot(heights, adjusted_weights);
    return material;
}

#endif
