#ifndef CUBEY_ATMOSPHERE_COMMON_GLSL
#define CUBEY_ATMOSPHERE_COMMON_GLSL

vec2 ray_sphere_intersection(vec3 ray_origin, vec3 ray_direction, vec3 sphere_center,
                             float sphere_radius) {
    return cubey_atmosphere_ray_sphere_intersection(ray_origin, ray_direction, sphere_center,
                                                    sphere_radius);
}

vec3 atmosphere_ray_origin() {
    return atmosphere.camera_position_radius.xyz;
}

vec3 atmosphere_planet_center() {
    return vec3(0.0);
}

vec3 atmosphere_camera_up(vec3 ray_origin, vec3 planet_center) {
    vec3 up = ray_origin - planet_center;
    if (dot(up, up) <= 0.000001) {
        return vec3(0.0, 1.0, 0.0);
    }
    return normalize(up);
}

CubeyAtmosphereMedium atmosphere_medium(vec3 planet_center) {
    return CubeyAtmosphereMedium(
        planet_center,
        atmosphere.radii_ground.x,
        atmosphere.radii_ground.y,
        atmosphere.rayleigh.xyz,
        atmosphere.rayleigh.w,
        atmosphere.mie.x,
        atmosphere.mie.y,
        atmosphere.mie.z,
        atmosphere.mie.w,
        atmosphere.ozone.xyz,
        atmosphere.ozone.w,
        atmosphere.atmosphere_options.x,
        normalize(atmosphere.sun_direction_radius.xyz),
        atmosphere.sun_direction_radius.w,
        vec3(ATMOSPHERE_SUN_INTENSITY),
        ATMOSPHERE_MIN_TWILIGHT_SOFTNESS);
}

vec3 transmittance_from_depth(CubeyAtmosphereOpticalDepth depth, vec3 planet_center) {
    return cubey_atmosphere_depth_transmittance(atmosphere_medium(planet_center), depth);
}

vec3 safe_horizontal_direction(vec3 direction, vec3 fallback) {
    vec3 horizontal = vec3(direction.x, 0.0, direction.z);
    float length_squared = dot(horizontal, horizontal);
    if (length_squared > 0.00000001) {
        return horizontal * inversesqrt(length_squared);
    }

    vec3 fallback_horizontal = vec3(fallback.x, 0.0, fallback.z);
    float fallback_length_squared = dot(fallback_horizontal, fallback_horizontal);
    if (fallback_length_squared > 0.00000001) {
        return fallback_horizontal * inversesqrt(fallback_length_squared);
    }
    return vec3(0.0, 0.0, 1.0);
}

CubeyAtmosphereOpticalDepth integrate_optical_depth(vec3 origin, vec3 direction, float ray_length,
                                                    vec3 planet_center, int sample_count) {
    return cubey_atmosphere_integrate_optical_depth(atmosphere_medium(planet_center), origin,
                                                   direction, 0.0, ray_length, sample_count);
}

float ground_sun_visibility(vec3 normal, vec3 sun_direction) {
    float softness =
        max(atmosphere.sun_direction_radius.w * 4.0, ATMOSPHERE_MIN_TWILIGHT_SOFTNESS);
    return smoothstep(-softness, softness, dot(normal, sun_direction));
}

float sun_elevation_degrees(vec3 sun_direction) {
    return degrees(asin(clamp(sun_direction.y, -1.0, 1.0)));
}

float twilight_visibility(float sun_elevation) {
    float astronomical_fade = smoothstep(-24.0, -6.0, sun_elevation);
    float daylight_fade = 1.0 - smoothstep(-1.0, 4.0, sun_elevation);
    return astronomical_fade * daylight_fade;
}

float star_visibility(float sun_elevation) {
    return 1.0 - smoothstep(-18.0, -6.0, sun_elevation);
}

CubeyAtmosphereSample integrate_atmosphere(vec3 ray_origin, vec3 ray_direction, float ray_start,
                                           float ray_end, vec3 planet_center) {
    return cubey_atmosphere_integrate_ray(atmosphere_medium(planet_center), ray_origin,
                                          ray_direction, ray_start, ray_end);
}

#endif // CUBEY_ATMOSPHERE_COMMON_GLSL
