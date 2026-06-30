#version 450
#extension GL_GOOGLE_include_directive : require

#include "cloud_ref_common.glsl"

layout(set = 0, binding = 1) uniform sampler2D cloud_product_texture;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_color;

vec4 cloud_ref_resolve_cloud_product(vec2 uv, float radius_px) {
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
    return total;
}

void main() {
    vec2 uv = frag_position * 0.5 + 0.5;
    int debug_view = int(params.ref_options.x + 0.5);
    bool final_view = debug_view == CLOUD_REF_DEBUG_FINAL;
    bool raw_final_view = debug_view == CLOUD_REF_DEBUG_RAW_FINAL;
    vec3 direction = cloud_ref_view_direction(frag_position);
    vec3 background = cloud_ref_background(direction);
    vec4 raw_cloud = texture(cloud_product_texture, uv);
    float blur_enabled = params.composite_options.x;
    float blur_strength = clamp(params.composite_options.y, 0.0, 1.0);
    float blur_radius_px = max(params.composite_options.z, 0.0);
    vec4 blurred_cloud = cloud_ref_resolve_cloud_product(uv, blur_radius_px);
    vec4 cloud = final_view && blur_enabled > 0.5
                     ? mix(raw_cloud, blurred_cloud, blur_strength)
                     : raw_cloud;
    vec3 color = cloud.rgb;
    if (debug_view == CLOUD_REF_DEBUG_BACKGROUND) {
        color = background;
    } else if (raw_final_view) {
        color = max(color, vec3(0.0));
    } else if (!final_view) {
        color = cloud.rgb;
    } else {
        color = cloud.rgb;
    }
    out_color = vec4(clamp(color, vec3(0.0), vec3(1.0)), 1.0);
}
