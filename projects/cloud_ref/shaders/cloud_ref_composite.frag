#version 450
#extension GL_GOOGLE_include_directive : require

#include "cloud_ref_common.glsl"

layout(set = 0, binding = 1) uniform sampler2D cloud_product_texture;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_color;

void main() {
    vec2 uv = frag_position * 0.5 + 0.5;
    int debug_view = int(params.ref_options.x + 0.5);
    vec3 direction = cloud_ref_view_direction(frag_position);
    vec3 background = cloud_ref_background(direction);
    vec4 cloud = texture(cloud_product_texture, uv);
    vec3 color = background * clamp(cloud.a, 0.0, 1.0) + cloud.rgb;
    if (debug_view == CLOUD_REF_DEBUG_BACKGROUND) {
        color = background;
    } else if (debug_view != CLOUD_REF_DEBUG_FINAL) {
        color = cloud.rgb;
    }
    out_color = vec4(cloud_ref_tonemap(color), 1.0);
}
