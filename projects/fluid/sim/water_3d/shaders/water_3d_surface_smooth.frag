#version 450
#extension GL_GOOGLE_include_directive : require

#include "water_3d_surface_common.glsl"

WATER3D_SURFACE_PARAMS;

#include "water_3d_surface_helpers.glsl"

layout(set = 0, binding = 0) uniform sampler2D source_surface;

layout(location = 0) in vec2 frag_uv;
layout(location = 0) out vec4 out_surface;

float bilateral_weight(float spatial_offset, float spatial_sigma, float depth_delta,
                       float depth_sigma) {
    float spatial = exp(-0.5 * (spatial_offset * spatial_offset) /
                        max(0.0001, spatial_sigma * spatial_sigma));
    float depth = exp(-abs(depth_delta) / max(0.0001, depth_sigma));
    return spatial * depth;
}

void main() {
    vec4 center = texture(source_surface, frag_uv);
    if (!water_surface_has_depth(center.x)) {
        out_surface = center;
        return;
    }

    vec2 texel_size = 1.0 / vec2(textureSize(source_surface, 0));
    vec2 direction = surface_params.particle_options.zw;
    int radius = int(clamp(surface_params.surface_options.x, 0.0,
                           float(WATER3D_SURFACE_MAX_SMOOTH_RADIUS)));
    if (radius <= 0 || dot(direction, direction) <= 0.0) {
        out_surface = center;
        return;
    }

    float spatial_sigma = max(1.0, float(radius) * 0.55);
    float depth_sigma = max(0.0001, surface_params.surface_options.y);
    float weight_sum = 0.0;
    float depth_sum = 0.0;
    float thickness_sum = 0.0;
    for (int sample_index = -WATER3D_SURFACE_MAX_SMOOTH_RADIUS;
         sample_index <= WATER3D_SURFACE_MAX_SMOOTH_RADIUS; ++sample_index) {
        if (abs(sample_index) > radius) {
            continue;
        }
        vec2 sample_uv = frag_uv + direction * texel_size * float(sample_index);
        vec4 sample_value = texture(source_surface, sample_uv);
        if (!water_surface_has_depth(sample_value.x)) {
            continue;
        }
        float weight = bilateral_weight(float(sample_index), spatial_sigma,
                                        sample_value.x - center.x, depth_sigma);
        depth_sum += sample_value.x * weight;
        thickness_sum += sample_value.y * weight;
        weight_sum += weight;
    }

    if (weight_sum <= 0.0) {
        out_surface = center;
        return;
    }
    out_surface = vec4(depth_sum / weight_sum, thickness_sum / weight_sum, center.z, center.w);
}
