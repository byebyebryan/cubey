#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"

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
} ocean_features;

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
const float OCEAN_REFLECTANCE = 0.02;
const float OCEAN_FAR_ANTI_REPEAT_START = 220.0;
const float OCEAN_FAR_ANTI_REPEAT_END = 900.0;
const float OCEAN_ATMOSPHERE_REFLECTION_MAX_LOD = 4.0;
const float OCEAN_SHAPE_ANTI_REPEAT_WEIGHT = 0.32;
const float OCEAN_CASCADE_DISTANCE_FADE_START_WAVES = 10.0;
const float OCEAN_CASCADE_DISTANCE_FADE_END_WAVES = 34.0;
const float OCEAN_CASCADE_SURFACE_FADE_START_WAVES = 12.0;
const float OCEAN_CASCADE_SURFACE_FADE_END_WAVES = 40.0;
const float OCEAN_CASCADE_MESH_FULL_TILE_CELL_DIVISOR = 8.0;
const float OCEAN_CASCADE_MESH_ZERO_TILE_CELL_DIVISOR = 3.0;
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

float ocean_luminance(vec3 color) {
    return dot(max(color, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
}

vec3 ocean_primary_light_direction() {
    return normalize(ocean.sun_direction.xyz);
}

float ocean_primary_light_intensity() {
    float dynamic_intensity = max(ocean.sun_direction.w, 0.0);
    float light_strength =
        clamp(ocean_features.material_options.x, 0.0, 1.0) *
        clamp(ocean_features.material_options.w, 0.0, 1.0);
    return mix(1.0, dynamic_intensity, light_strength);
}

vec3 ocean_primary_light_color() {
    float daylight = smoothstep(0.18, 0.72, ocean_primary_light_intensity());
    vec3 moon = cubey_srgb_to_linear(vec3(0.58, 0.62, 0.74));
    vec3 sun = cubey_srgb_to_linear(vec3(1.0, 0.86, 0.58));
    return mix(moon, sun, daylight);
}

float ocean_surface_shape_strength() {
    return max(ocean_features.feature_options.x, 0.0);
}

float ocean_surface_foam_strength() {
    return max(ocean_features.feature_options.y, 0.0);
}

float ocean_foam_history_strength() {
    return max(ocean_features.feature_options.z, 0.0);
}

float ocean_detail_anti_repeat_strength() {
    return clamp(ocean_features.feature_options.w, 0.0, 1.0);
}

float ocean_terrain_foam_strength() {
    return max(ocean_features.feature_options2.x, 0.0);
}

float ocean_shape_fade_distance_scale() {
    return max(ocean_features.feature_options2.y, 0.001);
}

float ocean_atmosphere_material_strength() {
    return clamp(ocean_features.material_options.x, 0.0, 1.0);
}

float ocean_atmosphere_sky_strength() {
    return ocean_atmosphere_material_strength() * clamp(ocean_features.material_options.y, 0.0, 1.0);
}

float ocean_atmosphere_reflection_strength() {
    return ocean_atmosphere_material_strength() * clamp(ocean_features.material_options.z, 0.0, 1.0);
}

float ocean_atmosphere_light_strength() {
    return ocean_atmosphere_material_strength() * clamp(ocean_features.material_options.w, 0.0, 1.0);
}

float ocean_foam_lighting_strength() {
    return clamp(ocean_features.fade_options.x, 0.0, 1.0);
}

bool ocean_reference_shadow_enabled() {
    return ocean_features.fade_options.y > 0.5;
}

float ocean_reference_shadow_strength() {
    return clamp(ocean_features.fade_options.z, 0.0, 0.95);
}

float ocean_self_shadow_strength() {
    return clamp(ocean_features.self_shadow_options.x, 0.0, 1.0);
}

float ocean_self_shadow_distance() {
    return max(ocean_features.self_shadow_options.y, 0.001);
}

float ocean_self_shadow_bias() {
    return max(ocean_features.self_shadow_options.z, 0.0);
}

int ocean_self_shadow_steps() {
    return int(clamp(floor(ocean_features.self_shadow_options.w + 0.5), 1.0,
                     float(OCEAN_SELF_SHADOW_MAX_STEPS)));
}

float ocean_normal_fade_distance_scale() {
    return max(ocean_features.feature_options2.z, 0.001);
}

float ocean_foam_fade_distance_scale() {
    return max(ocean_features.feature_options2.w, 0.001);
}

bool ocean_terrain_fields_enabled() {
    return terrain_ocean.ranges_flags.w > 0.5;
}

float ocean_horizon_fog_strength() {
    return max(ocean.mesh_options.w, 0.0);
}

float ocean_surface_water_datum_y() {
    return ocean_features.surface_frame_options.x;
}

float ocean_surface_planet_radius_m() {
    return max(ocean_features.surface_frame_options.y, 0.001);
}

float ocean_surface_camera_altitude_m() {
    return max(ocean_features.surface_frame_options.z, 0.0);
}

float ocean_surface_horizon_distance_m() {
    return max(ocean_features.surface_frame_options.w, 0.001);
}

vec3 ocean_sky_radiance(vec3 direction) {
    vec3 dir = normalize(direction);
    vec3 atmosphere =
        max(textureLod(atmosphere_sky_radiance_texture, dir, 0.0).rgb, vec3(0.0));
    vec3 static_horizon = cubey_srgb_to_linear(vec3(0.38, 0.52, 0.70));
    vec3 static_zenith = cubey_srgb_to_linear(vec3(0.08, 0.18, 0.32));
    vec3 static_sky = mix(static_horizon, static_zenith, smoothstep(0.0, 0.82, dir.y));
    return mix(static_sky, atmosphere, ocean_atmosphere_sky_strength());
}

float ocean_direct_light_scale() {
    return clamp(ocean_primary_light_intensity() / 2.25, 0.0, 1.25);
}

bool ocean_reference_shadow_axis(float origin, float direction, float min_value, float max_value,
                                 inout float t_min, inout float t_max) {
    if (abs(direction) < 0.0001) {
        return origin >= min_value && origin <= max_value;
    }
    float near_t = (min_value - origin) / direction;
    float far_t = (max_value - origin) / direction;
    if (near_t > far_t) {
        float temp = near_t;
        near_t = far_t;
        far_t = temp;
    }
    t_min = max(t_min, near_t);
    t_max = min(t_max, far_t);
    return t_min <= t_max;
}

float ocean_reference_pillar_shadow(vec3 world_position, vec3 light_dir) {
    if (!ocean_reference_shadow_enabled() || ocean_reference_shadow_strength() <= 0.0 ||
        light_dir.y <= 0.002) {
        return 1.0;
    }

    vec2 offset = world_position.xz - OCEAN_REFERENCE_PILLAR_CENTER_XZ;
    vec2 light_xz = light_dir.xz;
    float origin_u = dot(offset, OCEAN_REFERENCE_PILLAR_AXIS_U);
    float origin_v = dot(offset, OCEAN_REFERENCE_PILLAR_AXIS_V);
    float direction_u = dot(light_xz, OCEAN_REFERENCE_PILLAR_AXIS_U);
    float direction_v = dot(light_xz, OCEAN_REFERENCE_PILLAR_AXIS_V);

    float t_min = 0.04;
    float t_max = 100000.0;
    bool hit =
        ocean_reference_shadow_axis(origin_u, direction_u, -OCEAN_REFERENCE_PILLAR_HALF_WIDTH,
                                    OCEAN_REFERENCE_PILLAR_HALF_WIDTH, t_min, t_max) &&
        ocean_reference_shadow_axis(origin_v, direction_v, -OCEAN_REFERENCE_PILLAR_HALF_WIDTH,
                                    OCEAN_REFERENCE_PILLAR_HALF_WIDTH, t_min, t_max) &&
        ocean_reference_shadow_axis(world_position.y, light_dir.y, OCEAN_REFERENCE_PILLAR_MIN_Y,
                                    OCEAN_REFERENCE_PILLAR_MAX_Y, t_min, t_max);
    return hit ? 1.0 - ocean_reference_shadow_strength() : 1.0;
}

float ocean_ambient_light_scale() {
    vec3 sky_up = ocean_sky_radiance(vec3(0.0, 1.0, 0.0));
    return clamp(ocean_luminance(sky_up) * 1.8 + ocean_primary_light_intensity() * 0.05,
                 0.015, 1.2);
}

vec2 terrain_ocean_field_uv(vec2 position) {
    return clamp((position - terrain_ocean.uv_transform.xy) * terrain_ocean.uv_transform.zw,
                 vec2(0.0), vec2(1.0));
}

vec4 sample_terrain_ocean_fields(vec2 position) {
    return texture(terrain_ocean_fields_texture, terrain_ocean_field_uv(position));
}

vec3 terrain_depth_color(float water_depth) {
    float value = clamp(water_depth / max(terrain_ocean.ranges_flags.x, 1.0), 0.0, 1.0);
    vec3 shallow = cubey_srgb_to_linear(vec3(0.34, 0.72, 0.68));
    vec3 deep = cubey_srgb_to_linear(vec3(0.02, 0.09, 0.24));
    return mix(shallow, deep, value);
}

vec3 terrain_shore_color(float shore_sdf) {
    float value = clamp(shore_sdf / max(terrain_ocean.ranges_flags.y, 1.0), -1.0, 1.0);
    vec3 water = cubey_srgb_to_linear(vec3(0.05, 0.18, 0.45));
    vec3 beach = cubey_srgb_to_linear(vec3(0.70, 0.58, 0.38));
    vec3 land = cubey_srgb_to_linear(vec3(0.20, 0.38, 0.20));
    return value < 0.0 ? mix(beach, water, -value) : mix(beach, land, value);
}

vec2 rotate2(vec2 value, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return vec2(c * value.x - s * value.y, s * value.x + c * value.y);
}

float detail_anti_repeat_angle(uint cascade, uint domain) {
    float slot = float(cascade);
    return domain == 0u ? 0.39 + slot * 0.271 : -0.83 - slot * 0.193;
}

float detail_anti_repeat_scale(uint cascade, uint domain) {
    float slot = float(cascade);
    return domain == 0u ? 1.27 + slot * 0.10 : max(0.48, 0.76 - slot * 0.035);
}

vec2 detail_anti_repeat_offset(uint cascade, uint domain) {
    float slot = float(cascade);
    return domain == 0u ? vec2(719.0 - slot * 147.0, -277.0 + slot * 73.0)
                        : vec2(-607.0 + slot * 89.0, 431.0 - slot * 127.0);
}

float hash12(vec2 value) {
    vec3 p = fract(vec3(value.xyx) * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float value_noise(vec2 value) {
    vec2 cell = floor(value);
    vec2 local = fract(value);
    local = local * local * (3.0 - 2.0 * local);

    float a = hash12(cell);
    float b = hash12(cell + vec2(1.0, 0.0));
    float c = hash12(cell + vec2(0.0, 1.0));
    float d = hash12(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, local.x), mix(c, d, local.x), local.y);
}

vec2 detail_anti_repeat_weights(uint cascade, vec2 position, float factor) {
    float seed = float(cascade) * 19.37;
    float first = value_noise(position * 0.0011 + vec2(seed, seed * 0.37));
    float second = value_noise(position * 0.00073 + vec2(seed + 41.0, seed - 13.0));
    return factor * vec2(mix(0.38, 0.72, first), mix(0.28, 0.58, second));
}

float foam_breakup_weight(uint cascade, vec2 position, float factor) {
    if (factor <= 0.0) {
        return 1.0;
    }
    float seed = float(cascade) * 31.19;
    float broad = value_noise(position * 0.00041 + vec2(seed, seed * 0.53));
    float mid = value_noise(position * 0.0017 + vec2(seed - 17.0, seed + 29.0));
    float breakup = mix(0.58, 1.22, broad) * mix(0.82, 1.12, mid);
    return mix(1.0, breakup, factor);
}

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

vec4 texture_bicubic(in sampler2D source_texture, in vec2 uv) {
    vec2 dims = vec2(textureSize(source_texture, 0).xy);
    vec2 dims_inv = 1.0 / dims;
    uv = uv * dims + 0.5;

    vec2 fuv = fract(uv);
    vec2 fuv2 = fuv * fuv;
    vec2 fuv3 = fuv2 * fuv;
    vec4 wx = vec4(-fuv3.x + fuv2.x * 3.0 - fuv.x * 3.0 + 1.0,
                   fuv3.x * 3.0 - fuv2.x * 6.0 + 4.0,
                   -fuv3.x * 3.0 + fuv2.x * 3.0 + fuv.x * 3.0 + 1.0,
                   fuv3.x) /
              6.0;
    vec4 wy = vec4(-fuv3.y + fuv2.y * 3.0 - fuv.y * 3.0 + 1.0,
                   fuv3.y * 3.0 - fuv2.y * 6.0 + 4.0,
                   -fuv3.y * 3.0 + fuv2.y * 3.0 + fuv.y * 3.0 + 1.0,
                   fuv3.y) /
              6.0;
    vec4 g = vec4(wx.xz + wx.yw, wy.xz + wy.yw);
    vec4 h = (vec4(wx.yw, wy.yw) / g + vec2(-1.5, 0.5).xyxy + floor(uv).xxyy) *
             dims_inv.xxyy;
    vec2 w = g.xz / (g.xz + g.yw);
    return mix(mix(texture(source_texture, h.yw), texture(source_texture, h.xw), w.x),
               mix(texture(source_texture, h.yz), texture(source_texture, h.xz), w.x), w.y);
}

vec4 sample_normal(uint cascade, vec2 uv, float pixels_per_meter) {
    if (cascade == 0u) {
        return mix(texture_bicubic(normal_cascade0_texture, uv),
                   texture(normal_cascade0_texture, uv), min(1.0, pixels_per_meter * 0.1));
    }
    if (cascade == 1u) {
        return mix(texture_bicubic(normal_cascade1_texture, uv),
                   texture(normal_cascade1_texture, uv), min(1.0, pixels_per_meter * 0.1));
    }
    if (cascade == 2u) {
        return mix(texture_bicubic(normal_cascade2_texture, uv),
                   texture(normal_cascade2_texture, uv), min(1.0, pixels_per_meter * 0.1));
    }
    if (cascade == 3u) {
        return mix(texture_bicubic(normal_cascade3_texture, uv),
                   texture(normal_cascade3_texture, uv), min(1.0, pixels_per_meter * 0.1));
    }
    return mix(texture_bicubic(normal_cascade4_texture, uv),
               texture(normal_cascade4_texture, uv), min(1.0, pixels_per_meter * 0.1));
}

vec4 sample_foam(uint cascade, vec2 uv, float pixels_per_meter) {
    if (cascade == 0u) {
        return mix(texture_bicubic(foam_cascade0_texture, uv), texture(foam_cascade0_texture, uv),
                   min(1.0, pixels_per_meter * 0.1));
    }
    if (cascade == 1u) {
        return mix(texture_bicubic(foam_cascade1_texture, uv), texture(foam_cascade1_texture, uv),
                   min(1.0, pixels_per_meter * 0.1));
    }
    if (cascade == 2u) {
        return mix(texture_bicubic(foam_cascade2_texture, uv), texture(foam_cascade2_texture, uv),
                   min(1.0, pixels_per_meter * 0.1));
    }
    if (cascade == 3u) {
        return mix(texture_bicubic(foam_cascade3_texture, uv), texture(foam_cascade3_texture, uv),
                   min(1.0, pixels_per_meter * 0.1));
    }
    return mix(texture_bicubic(foam_cascade4_texture, uv), texture(foam_cascade4_texture, uv),
               min(1.0, pixels_per_meter * 0.1));
}

bool ocean_detail_anti_repeat_enabled(float factor) {
    return factor > 0.0;
}

float cascade_surface_lod_weight(uint cascade, float dist) {
    return cascade_distance_lod_weight(cascade, dist, OCEAN_CASCADE_SURFACE_FADE_START_WAVES,
                                       OCEAN_CASCADE_SURFACE_FADE_END_WAVES,
                                       ocean_shape_fade_distance_scale());
}

vec4 sample_normal_foam_domain(uint cascade, vec2 position, float tile_length,
                               float pixels_per_meter, uint domain) {
    float angle = detail_anti_repeat_angle(cascade, domain);
    float scale = detail_anti_repeat_scale(cascade, domain);
    vec2 secondary_position =
        rotate2(position, angle) * scale + detail_anti_repeat_offset(cascade, domain);
    vec2 secondary_uv = secondary_position / tile_length;
    vec4 secondary_normal = sample_normal(cascade, secondary_uv, pixels_per_meter);
    vec4 secondary_foam = sample_foam(cascade, secondary_uv, pixels_per_meter);
    vec2 secondary_gradient = rotate2(secondary_normal.xy, -angle) * scale;
    return vec4(secondary_gradient, secondary_foam.rg);
}

vec4 sample_normal_foam_gradient(uint cascade, vec2 position, float tile_length,
                                 float pixels_per_meter, float anti_repeat_factor) {
    vec2 primary_uv = position / tile_length;
    vec4 primary_normal = sample_normal(cascade, primary_uv, pixels_per_meter);
    vec4 primary_foam = sample_foam(cascade, primary_uv, pixels_per_meter);
    vec4 primary = vec4(primary_normal.xy, primary_foam.rg);
    if (!ocean_detail_anti_repeat_enabled(anti_repeat_factor)) {
        return primary;
    }

    vec2 weights = detail_anti_repeat_weights(cascade, position, anti_repeat_factor);
    vec4 secondary0 = sample_normal_foam_domain(cascade, position, tile_length, pixels_per_meter, 0u);
    vec4 secondary1 = sample_normal_foam_domain(cascade, position, tile_length, pixels_per_meter, 1u);
    vec2 gradient =
        (primary.xy + secondary0.xy * weights.x + secondary1.xy * weights.y) /
        (1.0 + weights.x + weights.y);
    vec2 foam = vec2(1.0) - (vec2(1.0) - primary.zw) *
                               (vec2(1.0) - secondary0.zw * weights.x) *
                               (vec2(1.0) - secondary1.zw * weights.y);
    foam = clamp(foam * foam_breakup_weight(cascade, position, anti_repeat_factor), 0.0, 1.0);
    return vec4(gradient, foam);
}

OceanFoamData ocean_foam_data(float dist) {
    OceanFoamData data;
    data.gradient = vec2(0.0);
    data.total = vec2(0.0);
    data.core = vec2(0.0);
    data.candidate = vec2(0.0);
    data.detail = vec2(0.0);

    float anti_repeat_factor =
        ocean_detail_anti_repeat_strength() *
        smoothstep(OCEAN_FAR_ANTI_REPEAT_START, OCEAN_FAR_ANTI_REPEAT_END, dist);
    for (uint cascade = 0u; cascade < 5u; ++cascade) {
        if (!ocean_cascade_enabled(cascade)) {
            continue;
        }
        float tile_length = max(cascade_tile_length(cascade), 0.001);
        float pixels_per_meter = cascade_map_size(cascade) / tile_length;
        vec4 normal_foam =
            sample_normal_foam_gradient(cascade, frag_sample_position, tile_length,
                                        pixels_per_meter, anti_repeat_factor);
        float normal_scale = cascade_normal_scale(cascade) * ocean_surface_shape_strength();
        float lod_weight = cascade_surface_lod_weight(cascade, dist);
        vec2 weighted_foam = normal_foam.zw * lod_weight * ocean_surface_foam_strength();
        data.gradient += normal_foam.xy * normal_scale * lod_weight;
        if (cascade < 2u) {
            data.core += weighted_foam;
        } else if (cascade < 4u) {
            data.candidate += weighted_foam;
        } else {
            data.detail += weighted_foam;
        }
        data.total += weighted_foam;
    }
    data.total = clamp(data.total, 0.0, 1.0);
    data.core = clamp(data.core, 0.0, 1.0);
    data.candidate = clamp(data.candidate, 0.0, 1.0);
    data.detail = clamp(data.detail, 0.0, 1.0);
    data.gradient *=
        mix(0.015, ocean.foam_color.w, exp(-dist * 0.0175 /
                                           ocean_normal_fade_distance_scale()));
    return data;
}

float ocean_material_distance_factor(float dist) {
    return smoothstep(250.0, 1800.0, dist);
}

float ocean_persistent_foam(float persistent, float dist) {
    return smoothstep(0.0, 1.0, persistent * 0.75) *
           exp(-dist * 0.0075 / ocean_foam_fade_distance_scale());
}

float ocean_current_foam_core(float current, float dist) {
    return smoothstep(0.20, 0.55, current) *
           exp(-dist * 0.010 / ocean_foam_fade_distance_scale());
}

float ocean_foam_signal(float persistent, float current, float dist) {
    return clamp(max(ocean_persistent_foam(persistent, dist),
                     ocean_current_foam_core(current, dist) * 0.25),
                 0.0, 1.0);
}

float ocean_foam_coverage(OceanFoamData foam_data, float dist, float ndotv) {
    float far_factor = ocean_material_distance_factor(dist / ocean_foam_fade_distance_scale());
    float density = max(ocean.inspection_options.z, 0.001);
    float sharpness = clamp(ocean.inspection_options.w, 0.0, 1.0);
    float history = ocean_persistent_foam(foam_data.total.x, dist) *
                    ocean_foam_history_strength();
    float current_core = ocean_current_foam_core(foam_data.total.y, dist);

    float history_mask = pow(smoothstep(0.02, 0.68, history * density),
                             mix(0.95, 1.35, sharpness));
    float fresh_core = smoothstep(0.24, 0.72, current_core * density);
    float view_factor = mix(0.82, 1.0, smoothstep(0.05, 0.55, ndotv));
    float coverage = max(history_mask * 0.68, fresh_core * 0.28);
    return clamp(coverage * view_factor * mix(0.94, 0.44, far_factor),
                 0.0, 0.72);
}

vec3 ocean_lit_foam_color(vec3 foam_color, vec3 normal, float ndotl, float direct_shadow,
                          float dist) {
    float far_factor = ocean_material_distance_factor(dist);
    float ambient_light = ocean_ambient_light_scale();
    float direct_light = ocean_direct_light_scale() * direct_shadow;
    vec3 sky_light = ocean_sky_radiance(normal) * 0.10;
    float diffuse_light = ambient_light * (0.08 + 0.24 * clamp(normal.y, 0.0, 1.0)) +
                          direct_light * ndotl * 0.62;
    vec3 lit_foam = foam_color * diffuse_light + sky_light;
    vec3 far_foam = foam_color * (ambient_light * 0.18 + direct_light * 0.34) +
                    ocean_sky_radiance(vec3(0.0, 1.0, 0.0)) * 0.08;
    vec3 dynamic_foam = mix(lit_foam, far_foam, far_factor * 0.55);
    vec3 static_foam = foam_color * 0.62 + ocean_sky_radiance(vec3(0.0, 1.0, 0.0)) * 0.05;
    return mix(static_foam, dynamic_foam, ocean_foam_lighting_strength());
}

vec3 ocean_shaded_foam(vec3 water, vec3 foam_color, vec3 normal, float ndotl, float direct_shadow,
                       float coverage, float dist) {
    float far_factor = ocean_material_distance_factor(dist);
    float edge = smoothstep(0.04, 0.28, coverage) *
                 (1.0 - smoothstep(0.58, 0.92, coverage));
    vec3 foam_lighting = ocean_lit_foam_color(foam_color, normal, ndotl, direct_shadow, dist);
    vec3 wet_water = mix(water, water * 1.14 + foam_color * 0.08,
                         edge * mix(0.36, 0.18, far_factor));
    return mix(wet_water, foam_lighting, coverage);
}

float ocean_horizon_extinction_factor(vec3 view_dir, float dist) {
    float horizon_distance = max(ocean_surface_horizon_distance_m(), ocean.mesh_options.z);
    float distance_extinction =
        smoothstep(horizon_distance * 0.30, horizon_distance * 0.95, dist);
    float low_angle = 1.0 - smoothstep(0.035, 0.36, abs(view_dir.y));
    float angle_extinction =
        low_angle * smoothstep(horizon_distance * 0.18, horizon_distance * 0.72, dist);
    return clamp((distance_extinction * 0.86 + angle_extinction * 0.34) *
                     ocean_horizon_fog_strength(),
                 0.0, 0.96);
}

OceanAerialPerspective ocean_horizon_aerial_perspective(vec3 view_dir, float dist) {
    float extinction = ocean_horizon_extinction_factor(view_dir, dist);
    vec3 horizon_dir = normalize(vec3(-view_dir.x, 0.055, -view_dir.z));
    return OceanAerialPerspective(ocean_sky_radiance(horizon_dir), 1.0 - extinction);
}

vec3 ocean_apply_horizon_aerial_perspective(vec3 water, vec3 view_dir, float dist) {
    OceanAerialPerspective perspective = ocean_horizon_aerial_perspective(view_dir, dist);
    return water * perspective.transmittance +
           perspective.inscatter * (1.0 - perspective.transmittance);
}

vec3 ocean_environment_reflection(vec3 direction, float roughness) {
    vec3 dir = normalize(direction);
    vec3 reflection =
        textureLod(atmosphere_reflection_texture, dir,
                   clamp(roughness, 0.0, 1.0) * OCEAN_ATMOSPHERE_REFLECTION_MAX_LOD)
            .rgb;
    return mix(ocean_sky_radiance(dir), reflection, ocean_atmosphere_reflection_strength());
}

vec3 debug_height_color(float height) {
    float value = clamp(height * 0.08 + 0.5, 0.0, 1.0);
    vec3 low = cubey_srgb_to_linear(vec3(0.04, 0.18, 0.42));
    vec3 mid = cubey_srgb_to_linear(vec3(0.12, 0.65, 0.78));
    vec3 high = cubey_srgb_to_linear(vec3(0.96, 0.94, 0.78));
    return value < 0.5 ? mix(low, mid, value * 2.0) : mix(mid, high, (value - 0.5) * 2.0);
}

vec3 debug_lod_color(float level, float max_level) {
    float value = max_level > 0.0 ? clamp(level / max_level, 0.0, 1.0) : 0.0;
    vec3 near_color = cubey_srgb_to_linear(vec3(0.98, 0.62, 0.18));
    vec3 mid_color = cubey_srgb_to_linear(vec3(0.12, 0.68, 0.62));
    vec3 far_color = cubey_srgb_to_linear(vec3(0.22, 0.34, 0.88));
    return value < 0.5 ? mix(near_color, mid_color, value * 2.0)
                       : mix(mid_color, far_color, (value - 0.5) * 2.0);
}

float active_displacement_lod_weight(float dist, float mesh_cell_size) {
    float weight = 0.0;
    float active_count = 0.0;
    for (uint cascade = 0u; cascade < 5u; ++cascade) {
        if (!ocean_cascade_enabled(cascade) || cascade_displacement_scale(cascade) <= 0.0) {
            continue;
        }
        weight += cascade_displacement_lod_weight(cascade, dist, mesh_cell_size);
        active_count += 1.0;
    }
    return active_count > 0.0 ? weight / active_count : 0.0;
}

float triangle_wire_factor(vec3 barycentric) {
    vec3 width = max(fwidth(barycentric), vec3(0.0001));
    vec3 edge = smoothstep(width * 0.75, width * 1.75, barycentric);
    return 1.0 - min(min(edge.x, edge.y), edge.z);
}

void main() {
    uint view = uint(ocean.debug_options.x + 0.5);
    vec3 camera_position = ocean.camera_time.xyz;
    float dist = length(frag_sample_position - camera_position.xz);
    OceanFoamData foam_data = ocean_foam_data(dist);
    vec3 normal = normalize(vec3(-foam_data.gradient.x, 1.0, -foam_data.gradient.y));
    float foam_persistent = foam_data.total.x;
    float foam_current = foam_data.total.y;

    vec3 water_color = cubey_srgb_to_linear(ocean.water_color.rgb);
    vec3 foam_color = cubey_srgb_to_linear(ocean.foam_color.rgb);
    vec3 view_dir = normalize(camera_position - frag_world_position);
    vec3 sun_dir = ocean_primary_light_direction();
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
    float foam_coverage = max(ocean_foam_coverage(foam_data, dist, ndotv), terrain_shore_foam);
    float ambient_light = ocean_ambient_light_scale();
    float direct_light = ocean_direct_light_scale();
    float reference_shadow = ocean_reference_pillar_shadow(frag_world_position, sun_dir);
    float wave_shadow =
        ocean_wave_self_shadow(frag_sample_position, frag_world_position.y, sun_dir,
                               frag_mesh_cell_size);
    float direct_shadow = min(reference_shadow, wave_shadow);
    float shadowed_direct_light = direct_light * direct_shadow;
    float roughness = clamp(ocean.water_color.w, 0.02, 1.0);
    roughness = mix(roughness, max(roughness, 0.78), material_distance);

    float fresnel =
        mix(pow(1.0 - ndotv, 5.0 * exp(-2.69 * roughness)) /
                (1.0 + 22.7 * pow(roughness, 1.5)),
            1.0, OCEAN_REFLECTANCE);
    vec3 reflection = ocean_environment_reflection(reflection_dir, roughness);
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
