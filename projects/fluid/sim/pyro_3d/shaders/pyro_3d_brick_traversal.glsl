ivec3 occupancy_cell(vec3 position, ivec3 cell_count) {
    vec3 uv = clamp(position / pyro_domain_size(), vec3(0.0), vec3(1.0));
    return clamp(ivec3(floor(uv * vec3(textureSize(density_volume, 0)) / 8.0)), ivec3(0), cell_count - 1);
}

bool occupancy_is_empty(vec3 position, out ivec3 cell, out ivec3 cell_count) {
    cell_count = textureSize(occupancy_volume, 0);
    cell = occupancy_cell(position, cell_count);
    // The obstacle casts its own shadow even in otherwise empty material.
    float radius = params.obstacle_options.y;
    if (radius > 0.0) {
        vec3 voxel_size = pyro_domain_size() / vec3(textureSize(density_volume, 0));
        vec3 brick_min = vec3(cell * 8) * voxel_size;
        vec3 brick_max = min(vec3((cell + 1) * 8) * voxel_size, pyro_domain_size());
        vec3 center = vec3(0.5, params.obstacle_options.x, 0.5);
        vec3 offset = center - clamp(center, brick_min, brick_max);
        if (dot(offset, offset) <= radius * radius) return false;
    }
    return texelFetch(occupancy_volume, cell, 0).r < 0.5;
}

int samples_to_next_occupancy_cell(vec3 ray_origin, vec3 direction, float first_t,
                                   float step_length, int index, int sample_count,
                                   vec3 position, ivec3 cell, ivec3 cell_count) {
    float distance_to_boundary = 1.0e30;
    for (int axis = 0; axis < 3; ++axis) {
        float component = direction[axis];
        if (abs(component) <= 1.0e-8) {
            continue;
        }
        float boundary_cell = float(cell[axis] + (component > 0.0 ? 1 : 0));
        float boundary_position = boundary_cell * 8.0 * pyro_domain_size()[axis] / float(textureSize(density_volume, 0)[axis]);
        float candidate = (boundary_position - position[axis]) / component;
        if (candidate >= 0.0) {
            distance_to_boundary = min(distance_to_boundary, candidate);
        }
    }

    int advance = distance_to_boundary >= 1.0e29
                      ? sample_count - index
                      : max(1, int(min(ceil(distance_to_boundary / step_length), float(sample_count - index))));
    advance = min(advance, sample_count - index);

    // Match the original sample expression (including shadow jitter) while checking
    // the jump, so a rounded boundary cannot omit the first new-cell sample.
    while (advance > 1) {
        int previous_index = index + advance - 1;
        float previous_t = first_t + (float(previous_index) + 0.5) * step_length;
        vec3 previous_position = ray_origin + direction * previous_t;
        if (all(equal(occupancy_cell(previous_position, cell_count), cell))) {
            break;
        }
        --advance;
    }
    return max(advance, 1);
}
