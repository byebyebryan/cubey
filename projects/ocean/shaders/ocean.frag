#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"
#include "cubey/procedural/noise.glsl"

layout(set = 0, binding = 0) uniform sampler2D displacement_cascade0_texture;
layout(set = 0, binding = 1) uniform sampler2D displacement_cascade1_texture;
layout(set = 0, binding = 2) uniform sampler2D displacement_cascade2_texture;
layout(set = 0, binding = 3) uniform sampler2D displacement_cascade3_texture;
layout(set = 0, binding = 4) uniform sampler2D displacement_cascade4_texture;
layout(set = 0, binding = 5) uniform sampler2D normal_cascade0_texture;
layout(set = 0, binding = 6) uniform sampler2D normal_cascade1_texture;
layout(set = 0, binding = 7) uniform sampler2D normal_cascade2_texture;
layout(set = 0, binding = 8) uniform sampler2D normal_cascade3_texture;
layout(set = 0, binding = 9) uniform sampler2D normal_cascade4_texture;
layout(set = 0, binding = 10) uniform sampler2D foam_cascade0_texture;
layout(set = 0, binding = 11) uniform sampler2D foam_cascade1_texture;
layout(set = 0, binding = 12) uniform sampler2D foam_cascade2_texture;
layout(set = 0, binding = 13) uniform sampler2D foam_cascade3_texture;
layout(set = 0, binding = 14) uniform sampler2D foam_cascade4_texture;
layout(set = 0, binding = 15) uniform samplerCube atmosphere_reflection_texture;
layout(set = 0, binding = 16) uniform samplerCube atmosphere_sky_radiance_texture;
layout(set = 0, binding = 17) uniform sampler2D terrain_ocean_fields_texture;
layout(set = 0, binding = 18) uniform TerrainOceanFieldParams {
    vec4 uv_transform;
    vec4 ranges_flags;
} terrain_ocean;
layout(set = 0, binding = 19) uniform OceanFeatureParams {
    vec4 feature_options;
    vec4 feature_options2;
    vec4 material_options;
    vec4 fade_options;
    vec4 cascade_options;
    vec4 self_shadow_options;
    vec4 surface_frame_options;
    vec4 surface_curve_options;
    vec4 far_field_options;
    vec4 far_field_options2;
    vec4 far_detail_options;
    vec4 cloud_shadow_world_to_uv_x;
    vec4 cloud_shadow_world_to_uv_y;
    vec4 cloud_lighting_options;
} ocean_features;
layout(set = 0, binding = 20) uniform sampler2D foam_filtered_cascade0_level0_texture;
layout(set = 0, binding = 21) uniform sampler2D foam_filtered_cascade0_level1_texture;
layout(set = 0, binding = 22) uniform sampler2D foam_filtered_cascade0_level2_texture;
layout(set = 0, binding = 23) uniform sampler2D foam_filtered_cascade1_level0_texture;
layout(set = 0, binding = 24) uniform sampler2D foam_filtered_cascade1_level1_texture;
layout(set = 0, binding = 25) uniform sampler2D foam_filtered_cascade1_level2_texture;
layout(set = 0, binding = 26) uniform sampler2D foam_filtered_cascade2_level0_texture;
layout(set = 0, binding = 27) uniform sampler2D foam_filtered_cascade2_level1_texture;
layout(set = 0, binding = 28) uniform sampler2D foam_filtered_cascade2_level2_texture;
layout(set = 0, binding = 29) uniform sampler2D foam_filtered_cascade3_level0_texture;
layout(set = 0, binding = 30) uniform sampler2D foam_filtered_cascade3_level1_texture;
layout(set = 0, binding = 31) uniform sampler2D foam_filtered_cascade3_level2_texture;
layout(set = 0, binding = 32) uniform sampler2D foam_filtered_cascade4_level0_texture;
layout(set = 0, binding = 33) uniform sampler2D foam_filtered_cascade4_level1_texture;
layout(set = 0, binding = 34) uniform sampler2D foam_filtered_cascade4_level2_texture;
layout(set = 0, binding = 35) uniform sampler2D cloud_shadow_transmittance_texture;

layout(push_constant) uniform OceanParams {
    mat4 view_projection;
    vec4 camera_time;
    vec4 mesh_options;
    vec4 patch_bounds;
    vec4 sun_direction;
    vec4 debug_options;
    vec4 inspection_options;
    vec4 tile_lengths;
    vec4 displacement_scales;
    vec4 normal_scales;
    vec4 cascade4_options;
    vec4 water_color;
    vec4 foam_color;
} ocean;

layout(location = 0) in vec3 frag_world_position;
layout(location = 1) in vec3 frag_displacement;
layout(location = 2) in vec2 frag_sample_position;
layout(location = 3) in vec4 frag_wave;
layout(location = 4) in float frag_patch_alpha;
layout(location = 5) noperspective in vec3 frag_barycentric;
layout(location = 6) in float frag_mesh_cell_size;
layout(location = 7) in vec3 frag_surface_up;
layout(location = 8) in float frag_surface_curve_drop;

layout(location = 0) out vec4 out_color;

const uint OCEAN_VIEW_FINAL = 0u;
const uint OCEAN_VIEW_HEIGHT = 1u;
const uint OCEAN_VIEW_DISPLACEMENT = 2u;
const uint OCEAN_VIEW_NORMAL = 3u;
const uint OCEAN_VIEW_FOAM = 4u;
const uint OCEAN_VIEW_FOAM_SOURCE = 5u;
const uint OCEAN_VIEW_FOAM_HISTORY = 6u;
const uint OCEAN_VIEW_FOAM_CORE = 7u;
const uint OCEAN_VIEW_FOAM_CANDIDATE = 8u;
const uint OCEAN_VIEW_FOAM_DETAIL = 9u;
const uint OCEAN_VIEW_LOD = 10u;
const uint OCEAN_VIEW_SKY_RADIANCE = 11u;
const uint OCEAN_VIEW_REFLECTION = 12u;
const uint OCEAN_VIEW_DIRECT_LIGHT = 13u;
const uint OCEAN_VIEW_AMBIENT_LIGHT = 14u;
const uint OCEAN_VIEW_EXPOSURE = 15u;
const uint OCEAN_VIEW_FOAM_RAW = 16u;
const uint OCEAN_VIEW_FOAM_LIT = 17u;
const uint OCEAN_VIEW_TERRAIN_DEPTH = 18u;
const uint OCEAN_VIEW_TERRAIN_SHORE = 19u;
const uint OCEAN_VIEW_TERRAIN_SLOPE = 20u;
const uint OCEAN_VIEW_CURVATURE = 21u;
const uint OCEAN_VIEW_FOOTPRINT = 22u;
const uint OCEAN_VIEW_ENERGY_LOD = 23u;
const uint OCEAN_VIEW_FOAM_FILTERED = 24u;
const uint OCEAN_VIEW_FAR_FIELD = 25u;
const uint OCEAN_VIEW_CLOUD_SHADOW = 26u;
const float OCEAN_REFLECTANCE = 0.02;
const float OCEAN_FAR_ANTI_REPEAT_START = 220.0;
const float OCEAN_FAR_ANTI_REPEAT_END = 900.0;
const float OCEAN_ATMOSPHERE_REFLECTION_MAX_LOD = 4.0;
const float OCEAN_SHAPE_ANTI_REPEAT_WEIGHT = 0.32;
const float OCEAN_CASCADE_DISTANCE_FADE_START_WAVES = 8.0;
const float OCEAN_CASCADE_DISTANCE_FADE_END_WAVES = 24.0;
const float OCEAN_CASCADE_SURFACE_FADE_START_WAVES = 10.0;
const float OCEAN_CASCADE_SURFACE_FADE_END_WAVES = 30.0;
const float OCEAN_CASCADE_MESH_FULL_TILE_CELL_DIVISOR = 10.0;
const float OCEAN_CASCADE_MESH_ZERO_TILE_CELL_DIVISOR = 4.0;
const int OCEAN_SELF_SHADOW_MAX_STEPS = 24;
const vec2 OCEAN_REFERENCE_PILLAR_CENTER_XZ = vec2(-24.0, 10.0);
const vec2 OCEAN_REFERENCE_PILLAR_AXIS_U = vec2(0.70710678, 0.70710678);
const vec2 OCEAN_REFERENCE_PILLAR_AXIS_V = vec2(-0.70710678, 0.70710678);
const float OCEAN_REFERENCE_PILLAR_HALF_WIDTH = 0.5;
const float OCEAN_REFERENCE_PILLAR_MIN_Y = -25.0;
const float OCEAN_REFERENCE_PILLAR_MAX_Y = 25.0;

struct OceanFoamData {
    vec2 gradient;
    vec2 total;
    vec2 core;
    vec2 candidate;
    vec2 detail;
};

struct OceanAerialPerspective {
    vec3 inscatter;
    float transmittance;
};

#include "ocean_shading.glsl"
#include "ocean_far_field.glsl"

float cascade_tile_length(uint cascade) {
    if (cascade == 0u) {
        return ocean.tile_lengths.x;
    }
    if (cascade == 1u) {
        return ocean.tile_lengths.y;
    }
    if (cascade == 2u) {
        return ocean.tile_lengths.z;
    }
    if (cascade == 3u) {
        return ocean.tile_lengths.w;
    }
    return ocean.cascade4_options.x;
}

float cascade_displacement_scale(uint cascade) {
    if (cascade == 0u) {
        return ocean.displacement_scales.x;
    }
    if (cascade == 1u) {
        return ocean.displacement_scales.y;
    }
    if (cascade == 2u) {
        return ocean.displacement_scales.z;
    }
    if (cascade == 3u) {
        return ocean.displacement_scales.w;
    }
    return ocean.cascade4_options.y;
}

float cascade_normal_scale(uint cascade) {
    if (cascade == 0u) {
        return ocean.normal_scales.x;
    }
    if (cascade == 1u) {
        return ocean.normal_scales.y;
    }
    if (cascade == 2u) {
        return ocean.normal_scales.z;
    }
    if (cascade == 3u) {
        return ocean.normal_scales.w;
    }
    return ocean.cascade4_options.z;
}

float shape_anti_repeat_angle(uint cascade) {
    return 0.47 + float(cascade) * 1.173;
}

vec2 shape_anti_repeat_offset(uint cascade) {
    float slot = float(cascade);
    return vec2(347.0 + slot * 193.0, -911.0 + slot * 467.0);
}

bool ocean_shape_anti_repeat_enabled() {
    return ocean.inspection_options.y > 0.0;
}

bool ocean_cascade_enabled(uint cascade) {
    float selected = ocean.inspection_options.x;
    int mask = int(ocean_features.cascade_options.x + 0.5);
    bool feature_enabled = (mask & (1 << int(cascade))) != 0;
    bool selected_enabled = selected < -0.5 || abs(selected - float(cascade)) < 0.5;
    return feature_enabled && selected_enabled;
}

float cascade_map_size(uint cascade) {
    if (cascade == 0u) {
        return ocean_features.cascade_options.y;
    }
    if (cascade == 1u) {
        return ocean_features.cascade_options.z;
    }
    if (cascade == 2u) {
        return ocean_features.cascade_options.w;
    }
    if (cascade == 3u) {
        return ocean_features.fade_options.w;
    }
    return ocean.cascade4_options.w;
}

vec4 sample_displacement(uint cascade, vec2 uv) {
    if (cascade == 0u) {
        return texture(displacement_cascade0_texture, uv);
    }
    if (cascade == 1u) {
        return texture(displacement_cascade1_texture, uv);
    }
    if (cascade == 2u) {
        return texture(displacement_cascade2_texture, uv);
    }
    if (cascade == 3u) {
        return texture(displacement_cascade3_texture, uv);
    }
    return texture(displacement_cascade4_texture, uv);
}

float sample_ocean_displacement_height(uint cascade, vec2 position, float tile_length) {
    float primary = sample_displacement(cascade, position / tile_length).y;
    if (!ocean_shape_anti_repeat_enabled()) {
        return primary;
    }

    float angle = shape_anti_repeat_angle(cascade);
    vec2 secondary_position = rotate2(position, angle) + shape_anti_repeat_offset(cascade);
    float secondary = sample_displacement(cascade, secondary_position / tile_length).y;
    float weight = OCEAN_SHAPE_ANTI_REPEAT_WEIGHT * clamp(ocean.inspection_options.y, 0.0, 1.0);
    return (primary + secondary * weight) / (1.0 + weight);
}

float cascade_distance_lod_weight(uint cascade, float camera_distance, float start_waves,
                                  float end_waves, float fade_scale) {
    float tile_length = max(cascade_tile_length(cascade), 0.001);
    float start = tile_length * start_waves * fade_scale;
    float end = tile_length * end_waves * fade_scale;
    return 1.0 - smoothstep(start, max(end, start + 0.001), camera_distance);
}

float cascade_mesh_lod_weight(uint cascade, float mesh_cell_size) {
    float tile_length = max(cascade_tile_length(cascade), 0.001);
    float full_cell = tile_length / OCEAN_CASCADE_MESH_FULL_TILE_CELL_DIVISOR;
    float zero_cell = tile_length / OCEAN_CASCADE_MESH_ZERO_TILE_CELL_DIVISOR;
    return 1.0 - smoothstep(full_cell, max(zero_cell, full_cell + 0.001),
                            max(mesh_cell_size, 0.001));
}

float cascade_displacement_lod_weight(uint cascade, float camera_distance, float mesh_cell_size) {
    float distance_weight =
        cascade_distance_lod_weight(cascade, camera_distance,
                                    OCEAN_CASCADE_DISTANCE_FADE_START_WAVES,
                                    OCEAN_CASCADE_DISTANCE_FADE_END_WAVES,
                                    ocean_shape_fade_distance_scale());
    return distance_weight * cascade_mesh_lod_weight(cascade, mesh_cell_size);
}

float horizon_displacement_weight(float camera_distance) {
    return min(exp(-(camera_distance - 150.0) * 0.007 /
                   ocean_shape_fade_distance_scale()),
               1.0);
}

float ocean_surface_height(vec2 position, float camera_distance, float mesh_cell_size) {
    float height = 0.0;
    for (uint cascade = 0u; cascade < 5u; ++cascade) {
        if (!ocean_cascade_enabled(cascade)) {
            continue;
        }
        float tile_length = max(cascade_tile_length(cascade), 0.001);
        height += sample_ocean_displacement_height(cascade, position, tile_length) *
                  cascade_displacement_scale(cascade) *
                  cascade_displacement_lod_weight(cascade, camera_distance, mesh_cell_size) *
                  ocean_surface_shape_strength();
    }
    return height * horizon_displacement_weight(camera_distance);
}

float ocean_wave_self_shadow(vec2 surface_position, float surface_height, vec3 light_dir,
                             float mesh_cell_size) {
    float strength = ocean_self_shadow_strength();
    if (strength <= 0.0 || light_dir.y <= 0.01) {
        return 1.0;
    }

    float horizontal_light = length(light_dir.xz);
    if (horizontal_light <= 0.001) {
        return 1.0;
    }

    vec2 step_direction = light_dir.xz / horizontal_light;
    float ray_slope = light_dir.y / horizontal_light;
    float max_distance = ocean_self_shadow_distance();
    float bias = ocean_self_shadow_bias();
    int steps = ocean_self_shadow_steps();
    float occlusion = 0.0;

    for (int step = 1; step <= OCEAN_SELF_SHADOW_MAX_STEPS; ++step) {
        if (step > steps) {
            break;
        }
        float step_factor = float(step) / float(steps);
        float ray_distance = max_distance * step_factor;
        vec2 sample_position = surface_position + step_direction * ray_distance;
        float camera_distance = length(sample_position - ocean.camera_time.xz);
        float sample_height = ocean_surface_height(sample_position, camera_distance, mesh_cell_size);
        float ray_height = surface_height + ray_distance * ray_slope + bias;
        float blocker = sample_height - ray_height;
        float softness = mix(0.05, 0.75, step_factor);
        occlusion = max(occlusion, smoothstep(0.0, softness, blocker));
    }

    return 1.0 - strength * occlusion;
}

#include "ocean_foam.glsl"
#include "ocean_debug.glsl"

void main() {
    uint view = uint(ocean.debug_options.x + 0.5);
    vec3 camera_position = ocean.camera_time.xyz;
    float dist = length(frag_sample_position - camera_position.xz);
    vec3 water_color = cubey_srgb_to_linear(ocean.water_color.rgb);
    vec3 foam_color = cubey_srgb_to_linear(ocean.foam_color.rgb);
    vec3 view_dir = normalize(camera_position - frag_world_position);
    vec3 sun_dir = ocean_primary_light_direction();
    float pixel_footprint_m = ocean_pixel_footprint_m();
    float far_detail_filter = ocean_far_detail_filter(dist, pixel_footprint_m);
    OceanFoamData foam_data = ocean_foam_data(dist, pixel_footprint_m);
    vec3 normal = normalize(frag_surface_up + vec3(-foam_data.gradient.x, 0.0,
                                                   -foam_data.gradient.y));
    float foam_persistent = foam_data.total.x;
    float foam_current = foam_data.total.y;

    float displacement_lod = active_displacement_lod_weight(dist, frag_mesh_cell_size);
    float surface_lod = active_surface_lod_weight(dist);
    float unresolved_lod_energy = active_unresolved_lod_energy(dist, frag_mesh_cell_size);
    float far_field_energy =
        ocean_far_field_factor(dist) * active_far_field_lod_energy(dist, frag_mesh_cell_size);
    float far_material_energy = max(far_field_energy, far_detail_filter * surface_lod);
    vec3 reflection_dir = reflect(-view_dir, normal);
    float ndotv = clamp(dot(normal, view_dir), 0.0, 1.0);
    float ndotl = clamp(dot(normal, sun_dir), 0.0, 1.0);
    float material_distance = ocean_material_distance_factor(dist);
    vec4 terrain_fields = sample_terrain_ocean_fields(frag_sample_position);
    float terrain_shore_foam =
        ocean_terrain_fields_enabled()
            ? (1.0 - smoothstep(0.0, 42.0, abs(terrain_fields.z))) *
                  (1.0 - smoothstep(0.5, 5.0, terrain_fields.y)) * 0.16 *
                  ocean_terrain_foam_strength()
            : 0.0;
    float near_foam_coverage = ocean_foam_coverage(foam_data, dist, ndotv);
    float foam_coverage = max(near_foam_coverage, terrain_shore_foam);
    float ambient_light = ocean_ambient_light_scale();
    float direct_light = ocean_direct_light_scale();
    float reference_shadow = ocean_reference_pillar_shadow(frag_world_position, sun_dir);
    float wave_shadow = ocean_wave_self_shadow(frag_sample_position,
                                               ocean_surface_water_datum_y() + frag_displacement.y,
                                               sun_dir, frag_mesh_cell_size);
    float cloud_transmittance = ocean_cloud_shadow_transmittance(frag_world_position);
    float cloud_shadow = ocean_cloud_shadow_factor(cloud_transmittance);
    float direct_shadow = min(min(reference_shadow, wave_shadow), cloud_shadow);
    float shadowed_direct_light = direct_light * direct_shadow;
    float roughness = clamp(ocean.water_color.w, 0.02, 1.0);
    roughness = mix(roughness, max(roughness, 0.78), material_distance);
    roughness = clamp(roughness + far_material_energy * ocean_far_roughness_strength(),
                      0.02, 1.0);

    float fresnel =
        mix(pow(1.0 - ndotv, 5.0 * exp(-2.69 * roughness)) /
                (1.0 + 22.7 * pow(roughness, 1.5)),
            1.0, OCEAN_REFLECTANCE);
    vec3 reflection = ocean_environment_reflection(reflection_dir, roughness);
    reflection *= ocean_far_reflection_variation(frag_sample_position, far_material_energy);
    vec3 ambient = water_color * (0.08 + 0.34 * ambient_light * clamp(normal.y, 0.0, 1.0)) +
                   ocean_sky_radiance(normal) * 0.08;
    float sss_height = max(0.0, frag_wave.x + 2.5) *
                       pow(max(dot(sun_dir, -view_dir), 0.0), 4.0) *
                       pow(0.5 - 0.5 * dot(sun_dir, normal), 3.0);
    float sss_near = 0.5 * pow(ndotv, 2.0);
    vec3 subsurface = (sss_height + sss_near) * cubey_srgb_to_linear(vec3(0.9, 1.15, 0.85));
    vec3 direct = water_color * (ambient_light * 0.05 + 0.72 * ndotl * shadowed_direct_light) +
                  subsurface * shadowed_direct_light * (1.0 - fresnel);

    vec3 halfway = normalize(sun_dir + view_dir);
    float specular =
        pow(max(dot(normal, halfway), 0.0), mix(24.0, 110.0, 1.0 - roughness)) * fresnel * 1.6;
    float far_glint =
        ocean_far_sun_glitter(reflection_dir, sun_dir, frag_sample_position, far_material_energy);
    specular += far_glint * fresnel;
    specular *= shadowed_direct_light * mix(1.0, 0.35, material_distance) *
                (1.0 - foam_coverage * 0.82);
    vec3 water = mix(ambient + direct, reflection, clamp(fresnel, 0.0, 0.92));
    water += ocean_primary_light_color() * specular;
    water = ocean_shaded_foam(water, foam_color, normal, ndotl, direct_shadow, foam_coverage,
                              dist);

    vec3 color = ocean_apply_horizon_aerial_perspective(water, view_dir, dist);

    if (view == OCEAN_VIEW_HEIGHT) {
        color = debug_height_color(frag_wave.x);
    } else if (view == OCEAN_VIEW_DISPLACEMENT) {
        color = cubey_srgb_to_linear(
            clamp(abs(frag_displacement) * vec3(0.08, 0.06, 0.08), vec3(0.0), vec3(1.0)));
    } else if (view == OCEAN_VIEW_NORMAL) {
        color = normal * 0.5 + 0.5;
    } else if (view == OCEAN_VIEW_FOAM) {
        color = vec3(foam_coverage);
    } else if (view == OCEAN_VIEW_FOAM_SOURCE) {
        color = vec3(foam_current);
    } else if (view == OCEAN_VIEW_FOAM_HISTORY) {
        color = vec3(foam_persistent);
    } else if (view == OCEAN_VIEW_FOAM_CORE) {
        color = vec3(foam_data.core.x);
    } else if (view == OCEAN_VIEW_FOAM_CANDIDATE) {
        color = vec3(foam_data.candidate.x);
    } else if (view == OCEAN_VIEW_FOAM_DETAIL) {
        color = vec3(foam_data.detail.x);
    } else if (view == OCEAN_VIEW_LOD) {
        float lod_support = active_displacement_lod_weight(dist, frag_mesh_cell_size);
        color = debug_lod_color(ocean.debug_options.y, ocean.debug_options.z) *
                mix(0.32, 1.12, lod_support);
    } else if (view == OCEAN_VIEW_SKY_RADIANCE) {
        color = ocean_sky_radiance(reflection_dir);
    } else if (view == OCEAN_VIEW_REFLECTION) {
        color = reflection;
    } else if (view == OCEAN_VIEW_DIRECT_LIGHT) {
        color = vec3(clamp(shadowed_direct_light / 1.25, 0.0, 1.0));
    } else if (view == OCEAN_VIEW_AMBIENT_LIGHT) {
        color = vec3(clamp(ambient_light / 1.2, 0.0, 1.0));
    } else if (view == OCEAN_VIEW_EXPOSURE) {
        color = vec3(clamp((ocean.debug_options.z + 4.0) / 8.0, 0.0, 1.0));
    } else if (view == OCEAN_VIEW_FOAM_RAW) {
        color = vec3(clamp(max(foam_persistent, foam_current), 0.0, 1.0));
    } else if (view == OCEAN_VIEW_FOAM_LIT) {
        color = ocean_lit_foam_color(foam_color, normal, ndotl, direct_shadow, dist) *
                max(foam_coverage, 0.035);
    } else if (view == OCEAN_VIEW_TERRAIN_DEPTH) {
        color = terrain_depth_color(terrain_fields.y);
    } else if (view == OCEAN_VIEW_TERRAIN_SHORE) {
        color = terrain_shore_color(terrain_fields.z);
    } else if (view == OCEAN_VIEW_TERRAIN_SLOPE) {
        color = vec3(clamp(terrain_fields.w / max(terrain_ocean.ranges_flags.z, 0.001), 0.0,
                           1.0));
    } else if (view == OCEAN_VIEW_CURVATURE) {
        color = debug_curvature_color(frag_surface_curve_drop);
    } else if (view == OCEAN_VIEW_FOOTPRINT) {
        color = debug_footprint_color(pixel_footprint_m);
    } else if (view == OCEAN_VIEW_ENERGY_LOD) {
        color = vec3(unresolved_lod_energy, displacement_lod, surface_lod);
    } else if (view == OCEAN_VIEW_FOAM_FILTERED) {
        vec2 filtered = filtered_foam_total(dist, pixel_footprint_m);
        color = vec3(filtered.x, filtered.y, max(filtered.x, filtered.y));
    } else if (view == OCEAN_VIEW_FAR_FIELD) {
        color = vec3(far_field_energy, far_material_energy, far_detail_filter);
    } else if (view == OCEAN_VIEW_CLOUD_SHADOW) {
        color = vec3(cloud_transmittance);
    }

    if (ocean.debug_options.w > 0.0) {
        float wire = triangle_wire_factor(frag_barycentric);
        vec3 wire_color = view == OCEAN_VIEW_LOD
                              ? cubey_srgb_to_linear(vec3(0.015, 0.020, 0.026))
                              : cubey_srgb_to_linear(vec3(0.82, 0.94, 1.0));
        color = mix(color, wire_color, wire * clamp(ocean.debug_options.w, 0.0, 1.0));
    }

    out_color = vec4(color, clamp(frag_patch_alpha, 0.0, 1.0));
}
