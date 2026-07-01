#version 450
#extension GL_GOOGLE_include_directive : require

#include "cloud_ref_common.glsl"

layout(set = 0, binding = 1) uniform sampler2D cloud_product_texture;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_color;

vec4 cloud_ref_resolve_cloud_layer(vec2 uv, float radius_px) {
    ivec2 size = textureSize(cloud_product_texture, 0);
    vec2 texel = radius_px / vec2(max(size, ivec2(1)));
    vec4 total = vec4(0.0);
    total += texture(cloud_product_texture, uv + vec2(-texel.x, texel.y)) * (1.0 / 16.0);
    total += texture(cloud_product_texture, uv + vec2(0.0, texel.y)) * (2.0 / 16.0);
    total += texture(cloud_product_texture, uv + vec2(texel.x, texel.y)) * (1.0 / 16.0);
    total += texture(cloud_product_texture, uv + vec2(-texel.x, 0.0)) * (2.0 / 16.0);
    total += texture(cloud_product_texture, uv) * (4.0 / 16.0);
    total += texture(cloud_product_texture, uv + vec2(texel.x, 0.0)) * (2.0 / 16.0);
    total += texture(cloud_product_texture, uv + vec2(-texel.x, -texel.y)) * (1.0 / 16.0);
    total += texture(cloud_product_texture, uv + vec2(0.0, -texel.y)) * (2.0 / 16.0);
    total += texture(cloud_product_texture, uv + vec2(texel.x, -texel.y)) * (1.0 / 16.0);
    return vec4(max(total.rgb, vec3(0.0)), clamp(total.a, 0.0, 1.0));
}

float cloud_ref_edge_coverage_weight(float alpha) {
    return smoothstep(0.004, 0.34, alpha) * (1.0 - smoothstep(0.66, 0.985, alpha));
}

vec4 cloud_ref_resolve_coverage_layer(vec2 uv, vec4 raw_layer, float radius_px) {
    ivec2 size = textureSize(cloud_product_texture, 0);
    vec2 base_texel = 1.0 / vec2(max(size, ivec2(1)));
    vec2 texel = base_texel * max(radius_px, 0.0);
    if (texel.x <= 0.0 || texel.y <= 0.0) {
        return raw_layer;
    }

    vec4 total = vec4(0.0);
    float total_weight = 0.0;
    float alpha_min = raw_layer.a;
    float alpha_max = raw_layer.a;
    float alpha_center = clamp(raw_layer.a, 0.0, 1.0);

    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 offset = vec2(float(x), float(y));
            float spatial = exp(-dot(offset, offset) * 0.32);
            vec4 sample_layer = texture(cloud_product_texture, uv + offset * texel);
            float sample_alpha = clamp(sample_layer.a, 0.0, 1.0);
            float weight = spatial;
            total += vec4(max(sample_layer.rgb, vec3(0.0)), sample_alpha) * weight;
            total_weight += weight;
            alpha_min = min(alpha_min, sample_alpha);
            alpha_max = max(alpha_max, sample_alpha);
        }
    }

    vec4 resolved = total / max(total_weight, 0.00001);
    resolved.a = clamp(resolved.a, 0.0, 1.0);
    float neighborhood_transition =
        smoothstep(0.006, 0.20, alpha_max) * (1.0 - smoothstep(0.70, 0.99, alpha_min));
    float alpha_variation = smoothstep(0.012, 0.22, alpha_max - alpha_min);
    float center_transition = max(cloud_ref_edge_coverage_weight(alpha_center),
                                  cloud_ref_edge_coverage_weight(resolved.a));
    float edge_weight =
        clamp(max(center_transition, neighborhood_transition * alpha_variation) * 1.15, 0.0, 1.0);
    return mix(raw_layer, resolved, edge_weight);
}

vec3 cloud_ref_composite_layer(vec3 background, vec4 layer) {
    float alpha = clamp(layer.a, 0.0, 1.0);
    return background * (1.0 - alpha) + max(layer.rgb, vec3(0.0));
}

vec3 cloud_ref_composite_sun_glare(vec3 direction, vec4 layer) {
    vec3 sun_dir = normalize(params.sun_direction_intensity.xyz);
    float sun_alignment = clamp(dot(sun_dir, direction), 0.0, 1.0);
    float alpha = clamp(layer.a, 0.0, 1.0);
    float transmittance = 1.0 - alpha;
    float cloud_presence = smoothstep(0.02, 0.24, alpha);
    float sun_visibility = smoothstep(0.08, 0.82, transmittance);
    float cloud_edge = cloud_ref_edge_coverage_weight(alpha);
    float sun_disk = pow(sun_alignment, 360.0) * 0.30;
    float sun_halo = pow(sun_alignment, 56.0) * 0.08;
    float glare = (sun_disk + sun_halo) * cloud_presence * sun_visibility *
                  (0.45 + 0.55 * cloud_edge) * clamp(params.final_options.w, 0.0, 3.0);
    return vec3(1.0, 0.58, 0.24) * glare;
}

vec3 cloud_ref_apply_final_shaping(vec3 color) {
    vec3 shaped = max(color, vec3(0.0));
    float luma = dot(shaped, vec3(0.2126, 0.7152, 0.0722));
    shaped = mix(vec3(luma), shaped, max(params.final_options.y, 0.0));
    shaped = (shaped - vec3(0.18)) * max(params.final_options.x, 0.0) + vec3(0.18);
    return max(shaped, vec3(0.0));
}

void main() {
    vec2 uv = frag_position * 0.5 + 0.5;
    int debug_view = int(params.ref_options.x + 0.5);
    bool final_view = debug_view == CLOUD_REF_DEBUG_FINAL;
    bool raw_final_view = debug_view == CLOUD_REF_DEBUG_RAW_FINAL;
    vec3 direction = cloud_ref_view_direction(frag_position);
    vec3 background = cloud_ref_background(direction);
    vec4 raw_layer = texture(cloud_product_texture, uv);
    float blur_enabled = params.composite_options.x;
    float blur_strength = clamp(params.composite_options.y, 0.0, 1.0);
    float blur_radius_px = max(params.composite_options.z, 0.0);
    int resolve_mode = int(params.composite_options.w + 0.5);
    vec4 resolved_layer =
        resolve_mode == CLOUD_REF_RESOLVE_METADATA_BILATERAL
            ? cloud_ref_resolve_coverage_layer(uv, raw_layer, blur_radius_px)
            : cloud_ref_resolve_cloud_layer(uv, blur_radius_px);
    vec4 layer = final_view && blur_enabled > 0.5
                     ? mix(raw_layer, resolved_layer, blur_strength)
                     : raw_layer;
    layer.a = clamp(layer.a, 0.0, 1.0);
    vec3 color = cloud_ref_composite_layer(background, layer);
    if (debug_view == CLOUD_REF_DEBUG_BACKGROUND) {
        color = background;
    } else if (raw_final_view) {
        color = cloud_ref_composite_layer(background, raw_layer);
    } else if (!final_view) {
        color = raw_layer.rgb;
    } else {
        color += cloud_ref_composite_sun_glare(direction, layer);
        color = cloud_ref_apply_final_shaping(color);
    }
    out_color = vec4(clamp(color, vec3(0.0), vec3(1.0)), 1.0);
}
