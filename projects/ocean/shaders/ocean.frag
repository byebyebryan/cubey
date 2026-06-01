#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"

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

layout(location = 0) out vec4 out_color;

const uint OCEAN_VIEW_FINAL = 0u;
const uint OCEAN_VIEW_HEIGHT = 1u;
const uint OCEAN_VIEW_DISPLACEMENT = 2u;
const uint OCEAN_VIEW_NORMAL = 3u;
const uint OCEAN_VIEW_FOAM = 4u;
const uint OCEAN_VIEW_FOAM_SOURCE = 5u;
const uint OCEAN_VIEW_FOAM_HISTORY = 6u;
const uint OCEAN_VIEW_FOAM_MACRO = 7u;
const uint OCEAN_VIEW_FOAM_CREST = 8u;
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

struct OceanFoamData {
    vec2 gradient;
    vec2 total;
    vec2 macro;
    vec2 crest;
    vec2 detail;
};

float ocean_luminance(vec3 color) {
    return dot(max(color, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
}

vec3 ocean_primary_light_direction() {
    return normalize(ocean.sun_direction.xyz);
}

float ocean_primary_light_intensity() {
    return max(ocean.sun_direction.w, 0.0);
}

vec3 ocean_primary_light_color() {
    float daylight = smoothstep(0.18, 0.72, ocean_primary_light_intensity());
    vec3 moon = cubey_srgb_to_linear(vec3(0.58, 0.62, 0.74));
    vec3 sun = cubey_srgb_to_linear(vec3(1.0, 0.86, 0.58));
    return mix(moon, sun, daylight);
}

bool ocean_terrain_fields_enabled() {
    return ocean.mesh_options.w < 0.0;
}

float ocean_horizon_fog_strength() {
    return abs(ocean.mesh_options.w);
}

vec3 ocean_sky_radiance(vec3 direction) {
    return max(textureLod(atmosphere_sky_radiance_texture, normalize(direction), 0.0).rgb,
               vec3(0.0));
}

float ocean_direct_light_scale() {
    return clamp(ocean_primary_light_intensity() / 2.25, 0.0, 1.25);
}

float ocean_ambient_light_scale() {
    vec3 sky_up = ocean_sky_radiance(vec3(0.0, 1.0, 0.0));
    return clamp(ocean_luminance(sky_up) * 1.8 + ocean_primary_light_intensity() * 0.05,
                 0.015, 1.2);
}

vec2 terrain_ocean_field_uv(vec2 position) {
    float extent = max(ocean.mesh_options.z * 2.0, 0.001);
    return clamp((position / extent) + vec2(0.5), vec2(0.0), vec2(1.0));
}

vec4 sample_terrain_ocean_fields(vec2 position) {
    return texture(terrain_ocean_fields_texture, terrain_ocean_field_uv(position));
}

vec3 terrain_depth_color(float water_depth) {
    float value = clamp(water_depth / max(ocean.water_color.w * 80.0 + 1.0, 1.0), 0.0, 1.0);
    vec3 shallow = cubey_srgb_to_linear(vec3(0.34, 0.72, 0.68));
    vec3 deep = cubey_srgb_to_linear(vec3(0.02, 0.09, 0.24));
    return mix(shallow, deep, value);
}

vec3 terrain_shore_color(float shore_sdf) {
    float value = clamp(shore_sdf / max(ocean.mesh_options.z * 0.35, 1.0), -1.0, 1.0);
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
    if (cascade == 1u) {
        return domain == 0u ? 0.39 : -0.83;
    }
    if (cascade == 2u) {
        return domain == 0u ? 0.73 : -1.17;
    }
    if (cascade == 3u) {
        return domain == 0u ? -0.58 : 1.29;
    }
    return domain == 0u ? 0.91 : -1.43;
}

float detail_anti_repeat_scale(uint cascade, uint domain) {
    if (cascade == 1u) {
        return domain == 0u ? 1.27 : 0.71;
    }
    if (cascade == 2u) {
        return domain == 0u ? 1.41 : 0.67;
    }
    if (cascade == 3u) {
        return domain == 0u ? 1.53 : 0.74;
    }
    return domain == 0u ? 1.67 : 0.58;
}

vec2 detail_anti_repeat_offset(uint cascade, uint domain) {
    if (cascade == 1u) {
        return domain == 0u ? vec2(719.0, -277.0) : vec2(-607.0, 431.0);
    }
    if (cascade == 2u) {
        return domain == 0u ? vec2(131.0, -389.0) : vec2(-521.0, 97.0);
    }
    if (cascade == 3u) {
        return domain == 0u ? vec2(-211.0, 307.0) : vec2(463.0, -173.0);
    }
    return domain == 0u ? vec2(47.0, 223.0) : vec2(-349.0, -421.0);
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
    if (cascade != 4u || factor <= 0.0) {
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

bool ocean_cascade_enabled(uint cascade) {
    float selected = ocean.inspection_options.x;
    return selected < -0.5 || abs(selected - float(cascade)) < 0.5;
}

bool ocean_detail_anti_repeat_enabled(uint cascade, float factor) {
    return cascade >= 1u && factor > 0.0;
}

float cascade_surface_lod_weight(uint cascade, float dist) {
    if (cascade == 0u) {
        return 1.0;
    }
    if (cascade == 1u) {
        return 1.0 - smoothstep(2600.0, 5600.0, dist);
    }
    if (cascade == 2u) {
        return 1.0 - smoothstep(1100.0, 3600.0, dist);
    }
    if (cascade == 3u) {
        return 1.0 - smoothstep(650.0, 2200.0, dist);
    }
    return 1.0 - smoothstep(260.0, 1200.0, dist);
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
    if (!ocean_detail_anti_repeat_enabled(cascade, anti_repeat_factor)) {
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
    data.macro = vec2(0.0);
    data.crest = vec2(0.0);
    data.detail = vec2(0.0);

    float map_size = ocean.cascade4_options.w;
    float anti_repeat_factor =
        clamp(ocean.inspection_options.y, 0.0, 1.0) *
        smoothstep(OCEAN_FAR_ANTI_REPEAT_START, OCEAN_FAR_ANTI_REPEAT_END, dist);
    for (uint cascade = 0u; cascade < 5u; ++cascade) {
        if (!ocean_cascade_enabled(cascade)) {
            continue;
        }
        float tile_length = max(cascade_tile_length(cascade), 0.001);
        float pixels_per_meter = map_size / tile_length;
        vec4 normal_foam =
            sample_normal_foam_gradient(cascade, frag_sample_position, tile_length, pixels_per_meter,
                                        anti_repeat_factor);
        float normal_scale = cascade_normal_scale(cascade);
        float lod_weight = cascade_surface_lod_weight(cascade, dist);
        vec2 weighted_foam = normal_foam.zw * lod_weight;
        data.gradient += normal_foam.xy * normal_scale * lod_weight;
        data.total += weighted_foam;
        if (cascade < 2u) {
            data.macro += weighted_foam;
        } else if (cascade < 4u) {
            data.crest += weighted_foam;
        } else {
            data.detail += weighted_foam;
        }
    }
    data.total = clamp(data.total, 0.0, 1.0);
    data.macro = clamp(data.macro, 0.0, 1.0);
    data.crest = clamp(data.crest, 0.0, 1.0);
    data.detail = clamp(data.detail, 0.0, 1.0);
    data.gradient *= mix(0.015, ocean.foam_color.w, exp(-dist * 0.0175));
    return data;
}

float ocean_material_distance_factor(float dist) {
    return smoothstep(250.0, 1800.0, dist);
}

float ocean_persistent_foam(float persistent, float dist) {
    return smoothstep(0.0, 1.0, persistent * 0.75) * exp(-dist * 0.0075);
}

float ocean_current_foam_core(float current, float dist) {
    return smoothstep(0.20, 0.55, current) * exp(-dist * 0.010);
}

float ocean_foam_signal(float persistent, float current, float dist) {
    return clamp(max(ocean_persistent_foam(persistent, dist),
                     ocean_current_foam_core(current, dist) * 0.25),
                 0.0, 1.0);
}

float ocean_foam_coverage(OceanFoamData foam_data, float dist, float ndotv) {
    float far_factor = ocean_material_distance_factor(dist);
    float density = max(ocean.inspection_options.z, 0.001);
    float sharpness = clamp(ocean.inspection_options.w, 0.0, 1.0);
    float macro_history = ocean_persistent_foam(foam_data.macro.x, dist);
    float crest_history = ocean_persistent_foam(foam_data.crest.x, dist);
    float detail_history = ocean_persistent_foam(foam_data.detail.x, dist);
    float crest_core = ocean_current_foam_core(foam_data.crest.y, dist);

    float macro = pow(clamp(macro_history * density * 0.95, 0.0, 1.0),
                      mix(1.10, 1.42, sharpness));
    float crest_input = crest_history * density * 1.04;
    float crest = pow(smoothstep(0.02, 0.65, crest_input),
                      mix(0.92, 1.18, sharpness));
    float fresh_core = smoothstep(0.28, 0.72, crest_core * density);
    float detail = pow(clamp(detail_history * density, 0.0, 1.0),
                       mix(1.35, 1.85, sharpness));
    float support = max(macro, crest);
    float detail_gate = smoothstep(0.05, 0.32, support);
    float coherent_crest = max(crest, fresh_core * 0.26) *
                           mix(0.92, 1.18, smoothstep(0.04, 0.42, macro));
    float view_factor = mix(0.82, 1.0, smoothstep(0.05, 0.55, ndotv));
    float coverage = max(macro * 0.24, coherent_crest * 0.70);
    coverage = max(coverage, detail * detail_gate * 0.18);
    return clamp(coverage * view_factor * mix(0.94, 0.44, far_factor),
                 0.0, 0.72);
}

vec3 ocean_lit_foam_color(vec3 foam_color, vec3 normal, float ndotl, float dist) {
    float far_factor = ocean_material_distance_factor(dist);
    float ambient_light = ocean_ambient_light_scale();
    float direct_light = ocean_direct_light_scale();
    vec3 sky_light = ocean_sky_radiance(normal) * 0.10;
    float diffuse_light = ambient_light * (0.08 + 0.24 * clamp(normal.y, 0.0, 1.0)) +
                          direct_light * ndotl * 0.62;
    vec3 lit_foam = foam_color * diffuse_light + sky_light;
    vec3 far_foam = foam_color * (ambient_light * 0.18 + direct_light * 0.34) +
                    ocean_sky_radiance(vec3(0.0, 1.0, 0.0)) * 0.08;
    return mix(lit_foam, far_foam, far_factor * 0.55);
}

vec3 ocean_shaded_foam(vec3 water, vec3 foam_color, vec3 normal, float ndotl, float coverage,
                       float dist) {
    float far_factor = ocean_material_distance_factor(dist);
    float edge = smoothstep(0.04, 0.28, coverage) *
                 (1.0 - smoothstep(0.58, 0.92, coverage));
    vec3 foam_lighting = ocean_lit_foam_color(foam_color, normal, ndotl, dist);
    vec3 wet_water = mix(water, water * 1.14 + foam_color * 0.08,
                         edge * mix(0.36, 0.18, far_factor));
    return mix(wet_water, foam_lighting, coverage);
}

float ocean_horizon_fog_factor(vec3 view_dir, float dist) {
    float distance_fog =
        smoothstep(ocean.mesh_options.z * 0.30, ocean.mesh_options.z * 0.95, dist);
    float low_angle = 1.0 - smoothstep(0.035, 0.36, abs(view_dir.y));
    float angle_fog =
        low_angle * smoothstep(ocean.mesh_options.z * 0.18, ocean.mesh_options.z * 0.72, dist);
    return clamp((distance_fog * 0.86 + angle_fog * 0.34) * ocean_horizon_fog_strength(), 0.0,
                 0.96);
}

vec3 ocean_environment_reflection(vec3 direction, float roughness) {
    return textureLod(atmosphere_reflection_texture, normalize(direction),
                      clamp(roughness, 0.0, 1.0) * OCEAN_ATMOSPHERE_REFLECTION_MAX_LOD).rgb;
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
                  (1.0 - smoothstep(0.5, 5.0, terrain_fields.y)) * 0.16
            : 0.0;
    float foam_coverage = max(ocean_foam_coverage(foam_data, dist, ndotv), terrain_shore_foam);
    float ambient_light = ocean_ambient_light_scale();
    float direct_light = ocean_direct_light_scale();
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
    vec3 direct = water_color * (ambient_light * 0.05 + 0.72 * ndotl * direct_light) +
                  subsurface * direct_light * (1.0 - fresnel);

    vec3 halfway = normalize(sun_dir + view_dir);
    float specular =
        pow(max(dot(normal, halfway), 0.0), mix(24.0, 110.0, 1.0 - roughness)) * fresnel * 1.6;
    specular *= direct_light * mix(1.0, 0.35, material_distance) * (1.0 - foam_coverage * 0.82);
    vec3 water = mix(ambient + direct, reflection, clamp(fresnel, 0.0, 0.92));
    water += ocean_primary_light_color() * specular;
    water = ocean_shaded_foam(water, foam_color, normal, ndotl, foam_coverage, dist);

    float horizon_fog = ocean_horizon_fog_factor(view_dir, dist);
    vec3 horizon_dir = normalize(vec3(-view_dir.x, 0.055, -view_dir.z));
    vec3 color = mix(water, ocean_sky_radiance(horizon_dir), horizon_fog);

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
    } else if (view == OCEAN_VIEW_FOAM_MACRO) {
        color = vec3(foam_data.macro.x);
    } else if (view == OCEAN_VIEW_FOAM_CREST) {
        color = vec3(foam_data.crest.x);
    } else if (view == OCEAN_VIEW_FOAM_DETAIL) {
        color = vec3(foam_data.detail.x);
    } else if (view == OCEAN_VIEW_LOD) {
        color = debug_lod_color(ocean.debug_options.y, ocean.debug_options.z);
    } else if (view == OCEAN_VIEW_SKY_RADIANCE) {
        color = ocean_sky_radiance(reflection_dir);
    } else if (view == OCEAN_VIEW_REFLECTION) {
        color = reflection;
    } else if (view == OCEAN_VIEW_DIRECT_LIGHT) {
        color = vec3(clamp(direct_light / 1.25, 0.0, 1.0));
    } else if (view == OCEAN_VIEW_AMBIENT_LIGHT) {
        color = vec3(clamp(ambient_light / 1.2, 0.0, 1.0));
    } else if (view == OCEAN_VIEW_EXPOSURE) {
        color = vec3(clamp((ocean.debug_options.z + 4.0) / 8.0, 0.0, 1.0));
    } else if (view == OCEAN_VIEW_FOAM_RAW) {
        color = vec3(clamp(max(foam_persistent, foam_current), 0.0, 1.0));
    } else if (view == OCEAN_VIEW_FOAM_LIT) {
        color = ocean_lit_foam_color(foam_color, normal, ndotl, dist) *
                max(foam_coverage, 0.035);
    } else if (view == OCEAN_VIEW_TERRAIN_DEPTH) {
        color = terrain_depth_color(terrain_fields.y);
    } else if (view == OCEAN_VIEW_TERRAIN_SHORE) {
        color = terrain_shore_color(terrain_fields.z);
    } else if (view == OCEAN_VIEW_TERRAIN_SLOPE) {
        color = vec3(clamp(terrain_fields.w, 0.0, 1.0));
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
