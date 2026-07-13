#ifndef CUBEY_OCEAN_FOAM_GLSL
#define CUBEY_OCEAN_FOAM_GLSL

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

float ocean_normal_presentation_scale(float dist) {
    return mix(0.015, ocean.foam_color.w,
               exp(-dist * 0.0175 / ocean_normal_fade_distance_scale()));
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

OceanFoamData ocean_foam_data(float dist, float footprint_m) {
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
    data.gradient *= ocean_normal_presentation_scale(dist);
    data.gradient *= mix(1.0, 0.08, ocean_far_detail_filter(dist, footprint_m));
    return data;
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

vec3 ocean_lit_foam_color(vec3 foam_color, vec3 normal, float direct_shadow, float dist) {
    float far_factor = ocean_material_distance_factor(dist);
    float ambient_light = ocean_ambient_light_scale();
    vec3 sky_light = ocean_sky_radiance(normal) * 0.10;
    float sun_ndotl = max(dot(normal, ocean_sun_light_direction()), 0.0);
    float moon_ndotl = max(dot(normal, ocean_moon_light_direction()), 0.0);
    vec3 directional_light =
        ocean_sun_light_color() * ocean_sun_light_scale() * sun_ndotl * direct_shadow +
        ocean_moon_light_color() * ocean_moon_light_scale() * moon_ndotl;
    vec3 diffuse_light =
        vec3(ambient_light * (0.08 + 0.24 * clamp(normal.y, 0.0, 1.0))) +
        directional_light * 0.62;
    vec3 lit_foam = foam_color * diffuse_light + sky_light;
    vec3 far_foam = foam_color * (vec3(ambient_light * 0.18) + directional_light * 0.34) +
                    ocean_sky_radiance(vec3(0.0, 1.0, 0.0)) * 0.08;
    vec3 dynamic_foam = mix(lit_foam, far_foam, far_factor * 0.55);
    vec3 static_foam = foam_color * 0.62 + ocean_sky_radiance(vec3(0.0, 1.0, 0.0)) * 0.05;
    return mix(static_foam, dynamic_foam, ocean_foam_lighting_strength());
}

vec3 ocean_shaded_foam(vec3 water, vec3 foam_color, vec3 normal, float direct_shadow,
                       float coverage, float dist) {
    float far_factor = ocean_material_distance_factor(dist);
    float edge = smoothstep(0.04, 0.28, coverage) *
                 (1.0 - smoothstep(0.58, 0.92, coverage));
    vec3 foam_lighting = ocean_lit_foam_color(foam_color, normal, direct_shadow, dist);
    vec3 wet_water = mix(water, water * 1.14 + foam_color * 0.08,
                         edge * mix(0.36, 0.18, far_factor));
    return mix(wet_water, foam_lighting, coverage);
}


#endif
