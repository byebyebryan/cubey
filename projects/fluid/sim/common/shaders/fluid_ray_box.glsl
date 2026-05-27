#ifndef CUBEY_FLUID_RAY_BOX_GLSL
#define CUBEY_FLUID_RAY_BOX_GLSL

float safe_ray_component(float value) {
    if (abs(value) >= 0.00001) {
        return value;
    }
    return value < 0.0 ? -0.00001 : 0.00001;
}

bool ray_box_intersection(vec3 origin, vec3 direction, out float near_t, out float far_t) {
    vec3 safe_direction = vec3(safe_ray_component(direction.x), safe_ray_component(direction.y),
                               safe_ray_component(direction.z));
    vec3 inv_direction = 1.0 / safe_direction;
    vec3 t0 = (vec3(0.0) - origin) * inv_direction;
    vec3 t1 = (vec3(1.0) - origin) * inv_direction;
    vec3 t_min = min(t0, t1);
    vec3 t_max = max(t0, t1);
    near_t = max(max(t_min.x, t_min.y), t_min.z);
    far_t = min(min(t_max.x, t_max.y), t_max.z);
    return far_t > max(near_t, 0.0);
}

float ray_box_exit_distance(vec3 origin, vec3 direction) {
    float near_t = 0.0;
    float far_t = 0.0;
    if (!ray_box_intersection(origin, direction, near_t, far_t)) {
        return 0.0;
    }
    return max(far_t, 0.0);
}

#endif
