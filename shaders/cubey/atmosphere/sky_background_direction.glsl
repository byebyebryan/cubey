#ifndef CUBEY_SKY_BACKGROUND_DIRECTION_GLSL
#define CUBEY_SKY_BACKGROUND_DIRECTION_GLSL

// Backdrop policy only: never use this remapping for reflected radiance or IBL.
vec3 atmosphere_camera_up(vec3 ray_origin, vec3 planet_center) {
    vec3 up = ray_origin - planet_center;
    if (dot(up, up) <= 0.000001) return vec3(0.0, 1.0, 0.0);
    return normalize(up);
}
vec3 horizon_direction_for_up(vec3 direction, vec3 local_up) {
    vec3 horizon = direction - local_up * dot(direction, local_up);
    float length_squared = dot(horizon, horizon);
    if (length_squared > 0.00000001) return horizon * inversesqrt(length_squared);
    vec3 fallback_axis = abs(local_up.y) < 0.95 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    return normalize(cross(fallback_axis, local_up));
}
vec3 sky_background_sample_direction(vec3 ray_direction, vec3 ray_origin, vec3 planet_center) {
    vec3 local_up = atmosphere_camera_up(ray_origin, planet_center);
    float ray_up = dot(ray_direction, local_up);
    vec3 horizon_direction = horizon_direction_for_up(ray_direction, local_up);
    float mirrored_up = max(abs(ray_up), 0.022);
    vec3 mirrored_direction = normalize(horizon_direction + local_up * mirrored_up);
    float horizon_blend = 1.0 - smoothstep(0.0, 0.080, ray_up);
    return normalize(mix(ray_direction, mirrored_direction, horizon_blend));
}
#endif
