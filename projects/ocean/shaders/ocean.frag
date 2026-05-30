#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"
#include "cubey/pbr.glsl"
#include "ocean_atmosphere.glsl"

layout(set = 0, binding = 5) uniform sampler2D normal_foam_cascade0_texture;
layout(set = 0, binding = 6) uniform sampler2D normal_foam_cascade1_texture;
layout(set = 0, binding = 7) uniform sampler2D normal_foam_cascade2_texture;
layout(set = 0, binding = 8) uniform sampler2D normal_foam_cascade3_texture;
layout(set = 0, binding = 9) uniform sampler2D normal_foam_cascade4_texture;

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
const uint OCEAN_VIEW_LOD = 5u;
const float OCEAN_REFLECTANCE = 0.02;

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

vec4 sample_normal_foam(uint cascade, vec2 uv, float pixels_per_meter) {
    if (cascade == 0u) {
        return mix(texture_bicubic(normal_foam_cascade0_texture, uv),
                   texture(normal_foam_cascade0_texture, uv), min(1.0, pixels_per_meter * 0.1));
    }
    if (cascade == 1u) {
        return mix(texture_bicubic(normal_foam_cascade1_texture, uv),
                   texture(normal_foam_cascade1_texture, uv), min(1.0, pixels_per_meter * 0.1));
    }
    if (cascade == 2u) {
        return mix(texture_bicubic(normal_foam_cascade2_texture, uv),
                   texture(normal_foam_cascade2_texture, uv), min(1.0, pixels_per_meter * 0.1));
    }
    if (cascade == 3u) {
        return mix(texture_bicubic(normal_foam_cascade3_texture, uv),
                   texture(normal_foam_cascade3_texture, uv), min(1.0, pixels_per_meter * 0.1));
    }
    return mix(texture_bicubic(normal_foam_cascade4_texture, uv),
               texture(normal_foam_cascade4_texture, uv), min(1.0, pixels_per_meter * 0.1));
}

bool ocean_cascade_enabled(uint cascade) {
    float selected = ocean.inspection_options.x;
    return selected < -0.5 || abs(selected - float(cascade)) < 0.5;
}

vec3 normal_foam_gradient(float dist) {
    vec3 gradient = vec3(0.0);
    float map_size = ocean.cascade4_options.w;
    for (uint cascade = 0u; cascade < 5u; ++cascade) {
        if (!ocean_cascade_enabled(cascade)) {
            continue;
        }
        float tile_length = max(cascade_tile_length(cascade), 0.001);
        vec2 uv = frag_sample_position / tile_length;
        float pixels_per_meter = map_size / tile_length;
        vec4 normal_foam = sample_normal_foam(cascade, uv, pixels_per_meter);
        float normal_scale = cascade_normal_scale(cascade);
        gradient += normal_foam.xyw * vec3(normal_scale, normal_scale, 1.0);
    }
    gradient.z = smoothstep(0.0, 1.0, gradient.z * 0.75) * exp(-dist * 0.0075);
    gradient.xy *= mix(0.015, ocean.foam_color.w, exp(-dist * 0.0175));
    return gradient;
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
    vec3 gradient = normal_foam_gradient(dist);
    vec3 normal = normalize(vec3(-gradient.x, 1.0, -gradient.y));
    float foam = clamp(gradient.z, 0.0, 1.0);

    vec3 water_color = cubey_srgb_to_linear(ocean.water_color.rgb);
    vec3 foam_color = cubey_srgb_to_linear(ocean.foam_color.rgb);
    vec3 view_dir = normalize(camera_position - frag_world_position);
    vec3 sun_dir = ocean_sun_direction();
    vec3 reflection_dir = reflect(-view_dir, normal);
    float ndotv = clamp(dot(normal, view_dir), 0.0, 1.0);
    float ndotl = clamp(dot(normal, sun_dir), 0.0, 1.0);
    float roughness = clamp(ocean.water_color.w, 0.02, 1.0);

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
    vec3 water = mix(ambient + direct, reflection, clamp(fresnel, 0.0, 0.92));
    water += cubey_srgb_to_linear(vec3(1.0, 0.78, 0.46)) * specular;
    water = mix(water, foam_color, foam);

    float horizon_fog =
        smoothstep(ocean.mesh_options.z * 0.28, ocean.mesh_options.z * 0.92, dist) *
        ocean.mesh_options.w;
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
        color = vec3(foam);
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
