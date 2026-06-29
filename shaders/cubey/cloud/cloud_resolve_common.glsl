float cloud_product_alpha(vec4 cloud) {
    return 1.0 - clamp(cloud.a, 0.0, 1.0);
}

float cloud_metadata_alpha(vec4 cloud, vec4 metadata) {
    return max(cloud_product_alpha(cloud), clamp(metadata.g, 0.0, 1.0));
}

float cloud_edge_resolve_strength() {
    return clamp(params.edge_options.z, 0.0, 1.0);
}

float cloud_alpha_at(vec2 uv) {
    return cloud_metadata_alpha(texture(cloud_product_texture, uv),
                                texture(cloud_metadata_texture, uv));
}

float cloud_alpha_gradient(vec2 uv, vec2 texel, float center_alpha) {
    float dx0 = abs(cloud_alpha_at(uv + vec2(texel.x, 0.0)) - center_alpha);
    float dx1 = abs(cloud_alpha_at(uv - vec2(texel.x, 0.0)) - center_alpha);
    float dy0 = abs(cloud_alpha_at(uv + vec2(0.0, texel.y)) - center_alpha);
    float dy1 = abs(cloud_alpha_at(uv - vec2(0.0, texel.y)) - center_alpha);
    return max(max(dx0, dx1), max(dy0, dy1));
}

float cloud_edge_mask(vec2 uv, vec4 cloud, vec4 metadata, vec3 direction, vec2 texel) {
    float alpha = cloud_metadata_alpha(cloud, metadata);
    float confidence = clamp(metadata.b, 0.0, 1.0);
    float horizon = pow(max(1.0 - abs(direction.y), 0.0), 2.0);
    float gradient = cloud_alpha_gradient(uv, texel, alpha);
    float lower_edge = smoothstep(0.004, 0.10, alpha);
    float upper_edge = 1.0 - smoothstep(0.58, 0.94, alpha);
    float alpha_change = smoothstep(0.012, 0.18, gradient);
    float confidence_boost = mix(1.20, 0.82, confidence);
    return clamp(lower_edge * upper_edge * alpha_change * confidence_boost *
                     mix(0.82, 1.24, horizon),
                 0.0,
                 1.0);
}

float cloud_resolve_neighbor_weight(vec4 center_cloud, vec4 center_metadata,
                                    vec4 sample_cloud, vec4 sample_metadata,
                                    ivec2 offset, float center_edge, float sample_edge) {
    float center_alpha = cloud_metadata_alpha(center_cloud, center_metadata);
    float sample_alpha = cloud_metadata_alpha(sample_cloud, sample_metadata);
    float offset_distance = length(vec2(offset));
    float kernel_weight = exp(-offset_distance * offset_distance * 0.30);
    if (max(center_alpha, sample_alpha) < 0.002) {
        return kernel_weight;
    }

    float edge_resolve = max(center_edge, sample_edge) * cloud_edge_resolve_strength();
    float alpha_sharpness = mix(mix(4.5, 8.0, center_alpha), 1.25, edge_resolve);
    float alpha_weight = exp(-abs(sample_alpha - center_alpha) * alpha_sharpness);
    float center_confidence = clamp(center_metadata.b, 0.0, 1.0);
    float sample_confidence = clamp(sample_metadata.b, 0.0, 1.0);
    float confidence = min(center_confidence, sample_confidence);
    float confidence_weight = mix(mix(0.22, 1.0, confidence),
                                  mix(0.52, 1.0, confidence),
                                  edge_resolve);

    float distance_weight = 1.0;
    float center_distance = max(center_metadata.r, 0.0);
    float sample_distance = max(sample_metadata.r, 0.0);
    if (confidence > 0.01 && max(center_alpha, sample_alpha) > 0.02) {
        float distance_scale =
            max(min(center_distance, sample_distance) * mix(0.024, 0.045, edge_resolve),
                mix(850.0, 1450.0, edge_resolve));
        distance_weight = exp(-abs(sample_distance - center_distance) / distance_scale);
    }
    return kernel_weight * alpha_weight * distance_weight * confidence_weight;
}

vec4 cloud_resolve_cloud_product(vec2 uv, vec3 direction) {
    ivec2 size = textureSize(cloud_product_texture, 0);
    vec2 texel = 1.0 / vec2(max(size, ivec2(1)));
    vec4 center = texture(cloud_product_texture, uv);
    vec4 center_metadata = texture(cloud_metadata_texture, uv);
    float horizon = pow(max(1.0 - abs(direction.y), 0.0), 2.0);
    float center_confidence = clamp(center_metadata.b, 0.0, 1.0);
    float center_edge = cloud_edge_mask(uv, center, center_metadata, direction, texel);
    float center_weight =
        mix(1.05, 2.05, center_confidence) * mix(0.82, 1.0, horizon) *
        mix(1.0, 0.68, center_edge * cloud_edge_resolve_strength());
    vec4 total = center * center_weight;
    float total_weight = center_weight;

    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            if (x == 0 && y == 0) {
                continue;
            }
            if (abs(x) == 2 && abs(y) == 2 && horizon < 0.35) {
                continue;
            }
            vec2 offset = vec2(float(x), float(y)) * texel;
            vec4 sample_value = texture(cloud_product_texture, uv + offset);
            vec4 sample_metadata = texture(cloud_metadata_texture, uv + offset);
            float sample_edge =
                cloud_edge_mask(uv + offset, sample_value, sample_metadata, direction, texel);
            float weight = cloud_resolve_neighbor_weight(center, center_metadata, sample_value,
                                                         sample_metadata, ivec2(x, y),
                                                         center_edge, sample_edge);
            weight *= mix(0.68, 1.0, horizon);
            total += sample_value * weight;
            total_weight += weight;
        }
    }
    return total / max(total_weight, 0.0001);
}

float cloud_resolve_strength(vec2 uv, vec4 raw_cloud, vec4 raw_metadata, vec3 direction) {
    ivec2 cloud_size = textureSize(cloud_product_texture, 0);
    vec2 cloud_texel = 1.0 / vec2(max(cloud_size, ivec2(1)));
    float edge_mask = cloud_edge_mask(uv, raw_cloud, raw_metadata, direction, cloud_texel);
    float base_resolve = clamp(params.composite_options.x, 0.0, 1.0);
    return clamp(base_resolve +
                     edge_mask * cloud_edge_resolve_strength() * (1.0 - base_resolve),
                 0.0,
                 1.0);
}

float cloud_resolve_edge_mask(vec2 uv, vec4 raw_cloud, vec4 raw_metadata, vec3 direction) {
    ivec2 cloud_size = textureSize(cloud_product_texture, 0);
    vec2 cloud_texel = 1.0 / vec2(max(cloud_size, ivec2(1)));
    return cloud_edge_mask(uv, raw_cloud, raw_metadata, direction, cloud_texel);
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
