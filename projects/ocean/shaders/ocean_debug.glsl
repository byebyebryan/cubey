#ifndef CUBEY_OCEAN_DEBUG_GLSL
#define CUBEY_OCEAN_DEBUG_GLSL

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

vec3 debug_curvature_color(float drop) {
    float value = clamp(-drop / max(ocean_surface_camera_altitude_m(), 1.0), 0.0, 1.0);
    vec3 flat_color = cubey_srgb_to_linear(vec3(0.05, 0.11, 0.18));
    vec3 mid_color = cubey_srgb_to_linear(vec3(0.10, 0.58, 0.82));
    vec3 far_color = cubey_srgb_to_linear(vec3(0.92, 0.78, 0.34));
    return value < 0.5 ? mix(flat_color, mid_color, value * 2.0)
                       : mix(mid_color, far_color, (value - 0.5) * 2.0);
}

float ocean_pixel_footprint_m() {
    vec2 dx = dFdx(frag_sample_position);
    vec2 dy = dFdy(frag_sample_position);
    float major_axis = max(length(dx), length(dy));
    float area = abs(dx.x * dy.y - dx.y * dy.x);
    return max(max(major_axis, sqrt(max(area, 0.0))), frag_mesh_cell_size);
}

vec3 debug_footprint_color(float footprint_m) {
    float value = clamp((log2(max(footprint_m, 0.001)) + 4.0) / 13.0, 0.0, 1.0);
    vec3 near_color = cubey_srgb_to_linear(vec3(0.08, 0.20, 0.62));
    vec3 mid_color = cubey_srgb_to_linear(vec3(0.15, 0.82, 0.64));
    vec3 far_color = cubey_srgb_to_linear(vec3(1.0, 0.72, 0.18));
    return value < 0.5 ? mix(near_color, mid_color, value * 2.0)
                       : mix(mid_color, far_color, (value - 0.5) * 2.0);
}

float active_surface_lod_weight(float dist) {
    float weight = 0.0;
    float active_count = 0.0;
    for (uint cascade = 0u; cascade < 5u; ++cascade) {
        if (!ocean_cascade_enabled(cascade) || cascade_normal_scale(cascade) <= 0.0) {
            continue;
        }
        weight += cascade_surface_lod_weight(cascade, dist);
        active_count += 1.0;
    }
    return active_count > 0.0 ? weight / active_count : 0.0;
}

float active_displacement_lod_weight(float dist, float mesh_cell_size) {
    float weight = 0.0;
    float active_count = 0.0;
    for (uint cascade = 0u; cascade < 5u; ++cascade) {
        if (!ocean_cascade_enabled(cascade) || cascade_displacement_scale(cascade) <= 0.0) {
            continue;
        }
        weight += cascade_displacement_lod_weight(cascade, dist, mesh_cell_size);
        active_count += 1.0;
    }
    return active_count > 0.0 ? weight / active_count : 0.0;
}

float active_unresolved_lod_energy(float dist, float mesh_cell_size) {
    float energy = 0.0;
    float scale_sum = 0.0;
    for (uint cascade = 0u; cascade < 5u; ++cascade) {
        float scale = max(cascade_normal_scale(cascade), cascade_displacement_scale(cascade));
        if (!ocean_cascade_enabled(cascade) || scale <= 0.0) {
            continue;
        }
        float surface_weight = cascade_surface_lod_weight(cascade, dist);
        float displacement_weight = cascade_displacement_lod_weight(cascade, dist, mesh_cell_size);
        energy += max(surface_weight - displacement_weight, 0.0) * scale;
        scale_sum += scale;
    }
    return scale_sum > 0.0 ? clamp(energy / scale_sum, 0.0, 1.0) : 0.0;
}

float active_far_field_lod_energy(float dist, float mesh_cell_size) {
    float energy = 0.0;
    float scale_sum = 0.0;
    for (uint cascade = 0u; cascade < 5u; ++cascade) {
        float scale = max(cascade_normal_scale(cascade), cascade_displacement_scale(cascade));
        if (!ocean_cascade_enabled(cascade) || scale <= 0.0) {
            continue;
        }
        float displacement_weight = cascade_displacement_lod_weight(cascade, dist, mesh_cell_size);
        energy += (1.0 - displacement_weight) * scale;
        scale_sum += scale;
    }
    return scale_sum > 0.0 ? clamp(energy / scale_sum, 0.0, 1.0) : 0.0;
}

float triangle_wire_factor(vec3 barycentric) {
    vec3 width = max(fwidth(barycentric), vec3(0.0001));
    vec3 edge = smoothstep(width * 0.75, width * 1.75, barycentric);
    return 1.0 - min(min(edge.x, edge.y), edge.z);
}


#endif
