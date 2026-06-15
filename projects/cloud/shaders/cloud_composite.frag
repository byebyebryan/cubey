#version 450
#extension GL_GOOGLE_include_directive : require

#include "cloud_common.glsl"

layout(set = 0, binding = 1) uniform sampler2D cloud_product_texture;
layout(set = 0, binding = 2) uniform sampler2D cloud_metadata_texture;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_color;

vec4 cloud_resolve_cloud_product(vec2 uv) {
    ivec2 size = textureSize(cloud_product_texture, 0);
    vec2 texel = 1.0 / vec2(max(size, ivec2(1)));
    vec4 center = texture(cloud_product_texture, uv);
    float center_alpha = 1.0 - clamp(center.a, 0.0, 1.0);
    vec4 total = center * 1.6;
    float total_weight = 1.6;

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            if (x == 0 && y == 0) {
                continue;
            }
            vec2 offset = vec2(float(x), float(y)) * texel;
            vec4 sample_value = texture(cloud_product_texture, uv + offset);
            float sample_alpha = 1.0 - clamp(sample_value.a, 0.0, 1.0);
            float edge_weight = exp(-abs(sample_alpha - center_alpha) * 5.5);
            float kernel_weight = (abs(x) + abs(y) == 1) ? 0.52 : 0.32;
            float weight = kernel_weight * edge_weight;
            total += sample_value * weight;
            total_weight += weight;
        }
    }
    return total / max(total_weight, 0.0001);
}

vec3 cloud_final_post(vec3 color, vec3 direction, float cloud_alpha) {
    vec3 sun_dir = normalize(params.sun_direction_intensity.xyz);
    float sun_alignment = max(dot(direction, sun_dir), 0.0);
    float horizon = pow(max(1.0 - abs(direction.y), 0.0), 3.0);
    float halo = pow(sun_alignment, 38.0) * params.sun_direction_intensity.w;
    float tight_glare = pow(sun_alignment, 420.0) * params.sun_direction_intensity.w;
    float glare_strength = clamp(params.lighting_strengths.w, 0.0, 3.0);
    float horizon_strength = clamp(params.composite_options.w, 0.0, 3.0);
    float contrast = max(params.composite_options.y, 0.0);
    float saturation = max(params.composite_options.z, 0.0);

    color += vec3(1.0, 0.58, 0.22) * halo * (0.10 + 0.16 * cloud_alpha) *
             glare_strength;
    color += vec3(1.0, 0.82, 0.50) * tight_glare * 1.25 * glare_strength;
    color += vec3(0.10, 0.12, 0.13) * horizon * (1.0 - cloud_alpha) * 0.22 *
             horizon_strength;
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luma), color, saturation);
    color = max((color - vec3(0.018)) * contrast, vec3(0.0));
    color = pow(max(color, vec3(0.0)), vec3(1.02));
    return color;
}

vec3 cloud_metadata_debug_color(vec2 uv, int debug_view) {
    vec4 metadata = texture(cloud_metadata_texture, uv);
    if (debug_view == CLOUD_DEBUG_METADATA_DISTANCE) {
        return vec3(clamp(metadata.r / 50000.0, 0.0, 1.0));
    }
    if (debug_view == CLOUD_DEBUG_METADATA_ALPHA) {
        return vec3(clamp(metadata.g, 0.0, 1.0));
    }
    if (debug_view == CLOUD_DEBUG_METADATA_CONFIDENCE) {
        return vec3(clamp(metadata.b, 0.0, 1.0));
    }
    if (debug_view == CLOUD_DEBUG_METADATA_DENSITY) {
        return vec3(clamp(metadata.a * 16.0, 0.0, 1.0));
    }
    return vec3(0.0);
}

void main() {
    vec2 uv = frag_position * 0.5 + 0.5;
    int debug_view = int(params.ref_options.x + 0.5);
    bool final_view = debug_view == CLOUD_DEBUG_FINAL;
    bool raw_final_view = debug_view == CLOUD_DEBUG_RAW_FINAL;
    vec3 direction = cloud_view_direction(frag_position);
    vec3 background = cloud_background(direction);
    vec4 raw_cloud = texture(cloud_product_texture, uv);
    vec4 resolved_cloud = cloud_resolve_cloud_product(uv);
    float resolve_strength = clamp(params.composite_options.x, 0.0, 1.0);
    vec4 cloud = final_view ? mix(raw_cloud, resolved_cloud, resolve_strength)
                            : raw_cloud;
    vec3 color = background * clamp(cloud.a, 0.0, 1.0) + cloud.rgb;
    if (debug_view == CLOUD_DEBUG_METADATA_DISTANCE ||
        debug_view == CLOUD_DEBUG_METADATA_ALPHA ||
        debug_view == CLOUD_DEBUG_METADATA_CONFIDENCE ||
        debug_view == CLOUD_DEBUG_METADATA_DENSITY) {
        color = cloud_metadata_debug_color(uv, debug_view);
    } else if (debug_view == CLOUD_DEBUG_BACKGROUND) {
        color = background;
    } else if (raw_final_view) {
        color = max(color, vec3(0.0));
    } else if (!final_view) {
        color = cloud.rgb;
    } else {
        color = cloud_final_post(color, direction, 1.0 - clamp(cloud.a, 0.0, 1.0));
    }
    out_color = vec4(cloud_tonemap(color), 1.0);
}
