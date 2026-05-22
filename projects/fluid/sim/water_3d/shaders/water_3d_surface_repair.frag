#version 450
#extension GL_GOOGLE_include_directive : require

#include "water_3d_surface_common.glsl"

WATER3D_SURFACE_PARAMS;

#include "water_3d_surface_helpers.glsl"

layout(set = 0, binding = 0) uniform sampler2D source_surface;

layout(location = 0) in vec2 frag_uv;
layout(location = 0) out vec4 out_surface;

float repair_weight(vec2 offset, float spatial_sigma, float depth_delta, float depth_sigma) {
    float spatial = exp(-0.5 * dot(offset, offset) / max(0.0001, spatial_sigma * spatial_sigma));
    float depth = exp(-abs(depth_delta) / max(0.0001, depth_sigma));
    return spatial * depth;
}

void main() {
    vec4 center = texture(source_surface, frag_uv);
    if (water_surface_has_depth(center.x)) {
        out_surface = center;
        return;
    }

    int fill_radius = int(clamp(floor(surface_params.surface_options.z + 0.5), 0.0,
                                float(WATER3D_SURFACE_MAX_FILL_RADIUS)));
    if (fill_radius <= 0) {
        out_surface = center;
        return;
    }

    vec2 texel_size = 1.0 / vec2(textureSize(source_surface, 0));
    float min_depth = WATER3D_SURFACE_DEPTH_SENTINEL;
    int valid_count = 0;
    for (int y = -WATER3D_SURFACE_MAX_FILL_RADIUS; y <= WATER3D_SURFACE_MAX_FILL_RADIUS; ++y) {
        for (int x = -WATER3D_SURFACE_MAX_FILL_RADIUS; x <= WATER3D_SURFACE_MAX_FILL_RADIUS; ++x) {
            vec2 offset = vec2(float(x), float(y));
            if (dot(offset, offset) > float(fill_radius * fill_radius) || (x == 0 && y == 0)) {
                continue;
            }
            vec4 sample_value = texture(source_surface, frag_uv + offset * texel_size);
            if (!water_surface_has_depth(sample_value.x)) {
                continue;
            }
            min_depth = min(min_depth, sample_value.x);
            ++valid_count;
        }
    }

    int required_support = max(3, fill_radius * 3);
    if (valid_count < required_support || !water_surface_has_depth(min_depth)) {
        out_surface = center;
        return;
    }

    float depth_sigma = max(0.0001, surface_params.surface_options.y);
    float depth_window = depth_sigma * 2.5;
    float spatial_sigma = max(1.0, float(fill_radius) * 0.75);
    float weight_sum = 0.0;
    float depth_sum = 0.0;
    float thickness_sum = 0.0;
    int close_support = 0;

    for (int y = -WATER3D_SURFACE_MAX_FILL_RADIUS; y <= WATER3D_SURFACE_MAX_FILL_RADIUS; ++y) {
        for (int x = -WATER3D_SURFACE_MAX_FILL_RADIUS; x <= WATER3D_SURFACE_MAX_FILL_RADIUS; ++x) {
            vec2 offset = vec2(float(x), float(y));
            if (dot(offset, offset) > float(fill_radius * fill_radius) || (x == 0 && y == 0)) {
                continue;
            }
            vec4 sample_value = texture(source_surface, frag_uv + offset * texel_size);
            if (!water_surface_has_depth(sample_value.x)) {
                continue;
            }

            float depth_delta = sample_value.x - min_depth;
            if (depth_delta > depth_window) {
                continue;
            }
            float weight = repair_weight(offset, spatial_sigma, depth_delta, depth_sigma);
            depth_sum += sample_value.x * weight;
            thickness_sum += sample_value.y * weight;
            weight_sum += weight;
            ++close_support;
        }
    }

    if (close_support < required_support || weight_sum <= 0.0) {
        out_surface = center;
        return;
    }

    float depth = depth_sum / weight_sum;
    float thickness = thickness_sum / weight_sum;
    out_surface = vec4(depth, thickness, thickness, depth);
}
