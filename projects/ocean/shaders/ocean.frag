#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"
#include "cubey/pbr.glsl"
#include "ocean_atmosphere.glsl"

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

layout(push_constant) uniform OceanParams {
    mat4 view_projection;
    vec4 camera_time;
    vec4 mesh_options;
    vec4 patch_bounds;
    vec4 display_transform;
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
const uint OCEAN_VIEW_LOD = 6u;
const float OCEAN_REFLECTANCE = 0.02;
const float OCEAN_FAR_ANTI_REPEAT_START = 220.0;
const float OCEAN_FAR_ANTI_REPEAT_END = 900.0;

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
    if (cascade == 0u || factor <= 0.0) {
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

vec4 normal_foam_gradient(float dist) {
    vec4 gradient = vec4(0.0);
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
        gradient += normal_foam * vec4(normal_scale, normal_scale, 1.0, 1.0) * lod_weight;
    }
    gradient.zw = clamp(gradient.zw, 0.0, 1.0);
    gradient.xy *= mix(0.015, ocean.foam_color.w, exp(-dist * 0.0175));
    return gradient;
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

float ocean_foam_coverage(float persistent, float current, float dist, float ndotv) {
    float far_factor = ocean_material_distance_factor(dist);
    float density = max(ocean.inspection_options.z, 0.001);
    float sharpness = clamp(ocean.inspection_options.w, 0.0, 1.0);
    float history = ocean_persistent_foam(persistent, dist) * density;
    float crest_core = ocean_current_foam_core(current, dist);
    float soft_foam = pow(clamp(history, 0.0, 1.0), mix(0.90, 1.25, sharpness));
    float fresh_core = smoothstep(0.35, 0.80, crest_core * density);
    float view_factor = mix(0.82, 1.0, smoothstep(0.05, 0.55, ndotv));
    return clamp(max(soft_foam * 0.72, fresh_core * 0.22) * view_factor *
                     mix(0.92, 0.36, far_factor),
                 0.0, 0.72);
}

vec3 ocean_shaded_foam(vec3 water, vec3 foam_color, vec3 normal, float ndotl, float foam,
                       float coverage, float dist) {
    float far_factor = ocean_material_distance_factor(dist);
    float edge = smoothstep(0.03, 0.30, foam) * (1.0 - smoothstep(0.62, 0.98, foam));
    vec3 sky_light = ocean_sky_color(normal) * 0.055;
    float diffuse_light = 0.36 + 0.18 * clamp(normal.y, 0.0, 1.0) + 0.28 * ndotl;
    vec3 lit_foam = foam_color * diffuse_light + sky_light;
    vec3 far_foam = foam_color * 0.58 + ocean_sky_color(vec3(0.0, 1.0, 0.0)) * 0.045;
    vec3 wet_water = mix(water, water * 1.14 + foam_color * 0.08,
                         edge * mix(0.36, 0.18, far_factor));
    return mix(wet_water, mix(lit_foam, far_foam, far_factor * 0.55), coverage);
}

float ocean_horizon_fog_factor(vec3 view_dir, float dist) {
    float distance_fog =
        smoothstep(ocean.mesh_options.z * 0.30, ocean.mesh_options.z * 0.95, dist);
    float low_angle = 1.0 - smoothstep(0.035, 0.36, abs(view_dir.y));
    float angle_fog =
        low_angle * smoothstep(ocean.mesh_options.z * 0.18, ocean.mesh_options.z * 0.72, dist);
    return clamp((distance_fog * 0.86 + angle_fog * 0.34) * ocean.mesh_options.w, 0.0, 0.96);
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

vec3 apply_display(vec3 color) {
    return cubey_pbr_apply_display_transform(color, ocean.display_transform);
}

void main() {
    uint view = uint(ocean.debug_options.x + 0.5);
    vec3 camera_position = ocean.camera_time.xyz;
    float dist = length(frag_sample_position - camera_position.xz);
    vec4 gradient = normal_foam_gradient(dist);
    vec3 normal = normalize(vec3(-gradient.x, 1.0, -gradient.y));
    float foam_persistent = clamp(gradient.z, 0.0, 1.0);
    float foam_current = clamp(gradient.w, 0.0, 1.0);
    float foam = ocean_foam_signal(foam_persistent, foam_current, dist);

    vec3 water_color = cubey_srgb_to_linear(ocean.water_color.rgb);
    vec3 foam_color = cubey_srgb_to_linear(ocean.foam_color.rgb);
    vec3 view_dir = normalize(camera_position - frag_world_position);
    vec3 sun_dir = ocean_sun_direction();
    vec3 reflection_dir = reflect(-view_dir, normal);
    float ndotv = clamp(dot(normal, view_dir), 0.0, 1.0);
    float ndotl = clamp(dot(normal, sun_dir), 0.0, 1.0);
    float material_distance = ocean_material_distance_factor(dist);
    float foam_coverage = ocean_foam_coverage(foam_persistent, foam_current, dist, ndotv);
    float roughness = clamp(ocean.water_color.w, 0.02, 1.0);
    roughness = mix(roughness, max(roughness, 0.78), material_distance);

    float fresnel =
        mix(pow(1.0 - ndotv, 5.0 * exp(-2.69 * roughness)) /
                (1.0 + 22.7 * pow(roughness, 1.5)),
            1.0, OCEAN_REFLECTANCE);
    vec3 reflection = ocean_sky_color(reflection_dir);
    vec3 ambient = water_color * (0.42 + 0.28 * normal.y) + ocean_sky_color(normal) * 0.08;
    float sss_height = max(0.0, frag_wave.x + 2.5) *
                       pow(max(dot(sun_dir, -view_dir), 0.0), 4.0) *
                       pow(0.5 - 0.5 * dot(sun_dir, normal), 3.0);
    float sss_near = 0.5 * pow(ndotv, 2.0);
    vec3 subsurface = (sss_height + sss_near) * cubey_srgb_to_linear(vec3(0.9, 1.15, 0.85));
    vec3 direct = water_color * (0.18 + 0.72 * ndotl) + subsurface * (1.0 - fresnel);

    vec3 halfway = normalize(sun_dir + view_dir);
    float specular =
        pow(max(dot(normal, halfway), 0.0), mix(24.0, 110.0, 1.0 - roughness)) * fresnel * 1.6;
    specular *= mix(1.0, 0.35, material_distance) * (1.0 - foam_coverage * 0.82);
    vec3 water = mix(ambient + direct, reflection, clamp(fresnel, 0.0, 0.92));
    water += cubey_srgb_to_linear(vec3(1.0, 0.78, 0.46)) * specular;
    water = ocean_shaded_foam(water, foam_color, normal, ndotl, foam, foam_coverage, dist);

    float horizon_fog = ocean_horizon_fog_factor(view_dir, dist);
    vec3 horizon_dir = normalize(vec3(-view_dir.x, 0.055, -view_dir.z));
    vec3 color = mix(water, ocean_sky_color(horizon_dir), horizon_fog);

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
    } else if (view == OCEAN_VIEW_LOD) {
        color = debug_lod_color(ocean.debug_options.y, ocean.debug_options.z);
    }

    if (ocean.debug_options.w > 0.0) {
        float wire = triangle_wire_factor(frag_barycentric);
        vec3 wire_color = view == OCEAN_VIEW_LOD
                              ? cubey_srgb_to_linear(vec3(0.015, 0.020, 0.026))
                              : cubey_srgb_to_linear(vec3(0.82, 0.94, 1.0));
        color = mix(color, wire_color, wire * clamp(ocean.debug_options.w, 0.0, 1.0));
    }

    out_color = vec4(apply_display(color), clamp(frag_patch_alpha, 0.0, 1.0));
}
